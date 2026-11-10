package api

import (
	"bufio"
	"fmt"
	"io"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/gin-gonic/gin"
)

// 有线电视小程序的频道表：服务端定期抓取公开直播源并裁剪成央视 + 卫视，
// 客户端启动时先用内置表秒开，再在后台静默拉取这里的线路，下次启动生效。
// 这样源失效时不用重新发版小程序。

const tvCacheTTL = 30 * time.Minute

var tvCCTVOrder = []string{
	"CCTV-1", "CCTV-2", "CCTV-3", "CCTV-4", "CCTV-5", "CCTV-5+",
	"CCTV-6", "CCTV-7", "CCTV-8", "CCTV-9", "CCTV-10", "CCTV-11",
	"CCTV-12", "CCTV-13", "CCTV-14", "CCTV-15", "CCTV-16", "CCTV-17",
}

var tvCCTVSub = map[string]string{
	"CCTV-1": "综合", "CCTV-2": "财经", "CCTV-3": "综艺",
	"CCTV-4": "中文国际", "CCTV-5": "体育", "CCTV-5+": "体育赛事",
	"CCTV-6": "电影", "CCTV-7": "国防军事", "CCTV-8": "电视剧",
	"CCTV-9": "纪录", "CCTV-10": "科教", "CCTV-11": "戏曲",
	"CCTV-12": "社会与法", "CCTV-13": "新闻", "CCTV-14": "少儿",
	"CCTV-15": "音乐", "CCTV-16": "奥林匹克", "CCTV-17": "农业农村",
}

var tvSatelliteOrder = []string{
	"湖南卫视", "浙江卫视", "江苏卫视", "东方卫视", "北京卫视",
	"广东卫视", "深圳卫视", "安徽卫视", "山东卫视", "湖北卫视",
	"河南卫视", "河北卫视", "天津卫视", "四川卫视", "重庆卫视",
	"辽宁卫视", "黑龙江卫视", "吉林卫视", "内蒙古卫视", "山西卫视",
	"陕西卫视", "甘肃卫视", "青海卫视", "宁夏卫视", "新疆卫视",
	"西藏卫视", "云南卫视", "贵州卫视", "广西卫视", "海南卫视",
	"江西卫视", "东南卫视", "三沙卫视", "香港卫视",
}

// tvM3USources 按顺序尝试，前一个失败自动换下一个。
var tvM3USources = []string{
	"https://cdn.jsdelivr.net/gh/Guovin/iptv-api@gd/output/result.m3u",
	"https://raw.githubusercontent.com/Guovin/iptv-api/gd/output/result.m3u",
}

type tvChannel struct {
	No    string   `json:"no"`
	Name  string   `json:"name"`
	Sub   string   `json:"sub"`
	Group string   `json:"group"`
	URLs  []string `json:"urls"`
}

type tvSnapshot struct {
	channels  []tvChannel
	fetchedAt time.Time
	source    string
}

var tvCache struct {
	sync.Mutex
	snap     *tvSnapshot
	inflight bool
}

// TVChannels 返回可直接播放的央视 + 卫视频道表。
// 首次请求会同步抓取；之后命中缓存，过期时先返回旧数据再后台刷新。
func (e *Env) TVChannels(c *gin.Context) {
	snap, err := tvSnapshotFor()
	if err != nil {
		Abort(c, http.StatusServiceUnavailable, "直播源暂时不可用")
		return
	}
	c.Header("Cache-Control", "public, max-age=300")
	c.JSON(http.StatusOK, gin.H{
		"updated_at": snap.fetchedAt.Unix(),
		"source":     snap.source,
		"channels":   snap.channels,
	})
}

func tvSnapshotFor() (*tvSnapshot, error) {
	tvCache.Lock()
	snap := tvCache.snap
	if snap != nil {
		fresh := time.Since(snap.fetchedAt) < tvCacheTTL
		if !fresh && !tvCache.inflight {
			tvCache.inflight = true
			go func() {
				defer func() {
					tvCache.Lock()
					tvCache.inflight = false
					tvCache.Unlock()
				}()
				if next, err := tvFetch(); err == nil {
					tvCache.Lock()
					tvCache.snap = next
					tvCache.Unlock()
				}
			}()
		}
		tvCache.Unlock()
		return snap, nil
	}
	tvCache.Unlock()

	// 冷启动：同一时间只让一个请求去抓，其余等结果。
	tvCache.Lock()
	defer tvCache.Unlock()
	if tvCache.snap != nil {
		return tvCache.snap, nil
	}
	next, err := tvFetch()
	if err != nil {
		return nil, err
	}
	tvCache.snap = next
	return next, nil
}

func tvFetch() (*tvSnapshot, error) {
	var lastErr error
	for _, src := range tvM3USources {
		body, err := tvDownload(src)
		if err != nil {
			lastErr = err
			continue
		}
		channels := tvParseM3U(body)
		if len(channels) < 10 {
			lastErr = fmt.Errorf("频道数量异常: %d", len(channels))
			continue
		}
		return &tvSnapshot{
			channels:  channels,
			fetchedAt: time.Now(),
			source:    src,
		}, nil
	}
	if lastErr == nil {
		lastErr = fmt.Errorf("没有可用的直播源")
	}
	return nil, lastErr
}

func tvDownload(src string) (string, error) {
	client := &http.Client{Timeout: 15 * time.Second}
	req, err := http.NewRequest(http.MethodGet, src, nil)
	if err != nil {
		return "", err
	}
	req.Header.Set("User-Agent", "NXMiniApp/1.0 (+https://ecfun.cc)")
	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	// 公开源正常在 1MB 以内，限制到 8MB 防止异常响应。
	data, err := io.ReadAll(io.LimitReader(resp.Body, 8<<20))
	if err != nil {
		return "", err
	}
	return string(data), nil
}

// tvParseM3U 从 M3U 中挑出央视与卫视，每个频道最多保留 3 条线路（优先 https）。
func tvParseM3U(body string) []tvChannel {
	urlsBy := make(map[string][]string)
	seen := make(map[string]bool)

	scanner := bufio.NewScanner(strings.NewReader(body))
	scanner.Buffer(make([]byte, 64*1024), 1<<20)
	name := ""
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		if strings.HasPrefix(line, "#EXTINF") {
			name = tvAttr(line, "tvg-name")
			if name == "" {
				if idx := strings.LastIndex(line, ","); idx >= 0 {
					name = strings.TrimSpace(line[idx+1:])
				}
			}
			continue
		}
		if strings.HasPrefix(line, "#") || name == "" {
			continue
		}
		if !tvWanted(name) || !strings.HasPrefix(line, "http") {
			continue
		}
		key := name + "\n" + line
		if seen[key] {
			continue
		}
		seen[key] = true
		urlsBy[name] = append(urlsBy[name], line)
	}

	channels := make([]tvChannel, 0, 64)
	no := 0
	for _, name := range tvCCTVOrder {
		urls := tvPickLines(urlsBy[name])
		if len(urls) == 0 {
			continue
		}
		no++
		channels = append(channels, tvChannel{
			No:    fmt.Sprintf("%03d", no),
			Name:  name,
			Sub:   tvCCTVSub[name],
			Group: "cctv",
			URLs:  urls,
		})
	}
	no = 100
	for _, name := range tvSatelliteOrder {
		urls := tvPickLines(urlsBy[name])
		if len(urls) == 0 {
			continue
		}
		no++
		channels = append(channels, tvChannel{
			No:    fmt.Sprintf("%03d", no),
			Name:  name,
			Sub:   "省级卫视",
			Group: "ws",
			URLs:  urls,
		})
	}
	return channels
}

func tvPickLines(urls []string) []string {
	httpsLines := make([]string, 0, 3)
	httpLines := make([]string, 0, 3)
	for _, u := range urls {
		if strings.HasPrefix(u, "https://") {
			httpsLines = append(httpsLines, u)
		} else {
			httpLines = append(httpLines, u)
		}
	}
	picked := append(httpsLines, httpLines...)
	if len(picked) > 3 {
		picked = picked[:3]
	}
	return picked
}

func tvWanted(name string) bool {
	if strings.HasPrefix(name, "CCTV-") {
		_, ok := tvCCTVSub[name]
		return ok
	}
	for _, n := range tvSatelliteOrder {
		if n == name {
			return true
		}
	}
	return false
}

func tvAttr(line, key string) string {
	marker := key + "=\""
	idx := strings.Index(line, marker)
	if idx < 0 {
		return ""
	}
	rest := line[idx+len(marker):]
	end := strings.Index(rest, "\"")
	if end < 0 {
		return ""
	}
	return strings.TrimSpace(rest[:end])
}
