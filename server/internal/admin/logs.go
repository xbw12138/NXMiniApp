package admin

import (
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/gin-gonic/gin"

	"nxminiapp/server/internal/api"
)

// deviceLogFile 是设备日志列表里的一项。
type deviceLogFile struct {
	ID        string `json:"id"`
	Device    string `json:"device"`
	Name      string `json:"name"`
	Size      int64  `json:"size"`
	UpdatedAt int64  `json:"updated_at"`
}

// resolveDeviceLog 把 "device/name" 形式的 id 解析成磁盘路径，拒绝路径穿越。
func resolveDeviceLog(root, id string) (string, bool) {
	parts := strings.Split(strings.Trim(id, "/"), "/")
	if len(parts) != 2 || parts[0] == "" || parts[1] == "" {
		return "", false
	}
	if strings.Contains(parts[0], "..") || strings.Contains(parts[1], "..") {
		return "", false
	}
	return filepath.Join(root, parts[0], parts[1]), true
}

func listDeviceLogs(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		root := e.Cfg.DeviceLogsDir()
		// 设备 ID 与文件名都支持筛选，便于从大量真机日志里定位。
		deviceFilter := strings.TrimSpace(c.Query("device"))
		nameFilter := strings.ToLower(strings.TrimSpace(c.Query("q")))
		items := make([]deviceLogFile, 0)
		devices, _ := os.ReadDir(root)
		for _, device := range devices {
			if !device.IsDir() {
				continue
			}
			if deviceFilter != "" && device.Name() != deviceFilter {
				continue
			}
			files, err := os.ReadDir(filepath.Join(root, device.Name()))
			if err != nil {
				continue
			}
			for _, f := range files {
				if f.IsDir() {
					continue
				}
				if nameFilter != "" && !strings.Contains(strings.ToLower(f.Name()), nameFilter) {
					continue
				}
				info, err := f.Info()
				if err != nil {
					continue
				}
				items = append(items, deviceLogFile{
					ID:        device.Name() + "/" + f.Name(),
					Device:    device.Name(),
					Name:      f.Name(),
					Size:      info.Size(),
					UpdatedAt: info.ModTime().UnixMilli(),
				})
			}
		}
		sort.Slice(items, func(i, j int) bool { return items[i].UpdatedAt > items[j].UpdatedAt })
		c.JSON(http.StatusOK, gin.H{"items": items})
	}
}

func getDeviceLog(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		path, ok := resolveDeviceLog(e.Cfg.DeviceLogsDir(), c.Query("id"))
		if !ok {
			api.Abort(c, http.StatusBadRequest, "日志标识无效")
			return
		}
		data, err := os.ReadFile(path)
		if err != nil {
			api.Abort(c, http.StatusNotFound, "日志不存在")
			return
		}
		// 只回传末尾 512KB，避免超长日志把后台页面拖死。
		const maxBytes = 512 << 10
		truncated := false
		if len(data) > maxBytes {
			data = data[len(data)-maxBytes:]
			truncated = true
		}
		c.JSON(http.StatusOK, gin.H{"content": string(data), "truncated": truncated})
	}
}

func deleteDeviceLog(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		path, ok := resolveDeviceLog(e.Cfg.DeviceLogsDir(), c.Query("id"))
		if !ok {
			api.Abort(c, http.StatusBadRequest, "日志标识无效")
			return
		}
		if err := os.Remove(path); err != nil {
			api.Abort(c, http.StatusNotFound, "日志不存在")
			return
		}
		c.JSON(http.StatusOK, gin.H{"ok": true})
	}
}
