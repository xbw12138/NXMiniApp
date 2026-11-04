package api

import (
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/gin-gonic/gin"

	"nxminiapp/server/internal/miniappstore"
	"nxminiapp/server/internal/models"
)

// 投稿限制：公开接口无鉴权，做最基本的大小与频率保护。
const (
	maxPackageSize = 32 << 20 // 小程序包上限 32MB
	maxIconSize    = 2 << 20  // 图标上限 2MB

	submitRateWindow = time.Hour
	submitRateMax    = 10 // 每 IP 每小时最多投稿次数
)

// submitLimiter 是一个按客户端 IP 划窗的内存限流器。
type submitLimiter struct {
	mu   sync.Mutex
	hits map[string][]time.Time
}

var submitRate = &submitLimiter{hits: map[string][]time.Time{}}

func (l *submitLimiter) allow(ip string) bool {
	now := time.Now()
	cutoff := now.Add(-submitRateWindow)
	l.mu.Lock()
	defer l.mu.Unlock()
	kept := l.hits[ip][:0]
	for _, t := range l.hits[ip] {
		if t.After(cutoff) {
			kept = append(kept, t)
		}
	}
	if len(kept) >= submitRateMax {
		l.hits[ip] = kept
		return false
	}
	l.hits[ip] = append(kept, now)
	return true
}

// slugID 从标题推导小程序 ID；无法得到有效前缀时退回随机串。
func slugID(title string) string {
	s := strings.ToLower(strings.TrimSpace(title))
	var b strings.Builder
	for _, r := range s {
		switch {
		case r >= 'a' && r <= 'z', r >= '0' && r <= '9':
			b.WriteRune(r)
		case r == '-' || r == '_':
			b.WriteRune(r)
		default:
			b.WriteByte('-')
		}
	}
	s = strings.Trim(b.String(), "-_")
	for strings.Contains(s, "--") {
		s = strings.ReplaceAll(s, "--", "-")
	}
	if len(s) > 48 {
		s = strings.Trim(s[:48], "-_")
	}
	if s == "" || !IDPattern.MatchString(s) {
		s = "app-" + RandomHex(4)
	}
	return s
}

// SubmitMiniApp 处理免登录投稿：校验元数据与安装包，落库为待审核（published=false）。
func (e *Env) SubmitMiniApp(c *gin.Context) {
	if !submitRate.allow(c.ClientIP()) {
		Abort(c, http.StatusTooManyRequests, "投稿过于频繁，请稍后再试")
		return
	}

	title := strings.TrimSpace(c.PostForm("title"))
	if title == "" {
		Abort(c, http.StatusBadRequest, "请填写小程序名称")
		return
	}
	id := strings.ToLower(strings.TrimSpace(c.PostForm("id")))
	if id == "" {
		id = slugID(title)
	} else if !IDPattern.MatchString(id) {
		Abort(c, http.StatusBadRequest, "小程序 ID 仅支持小写字母、数字、- 和 _，且长度 2-64")
		return
	}
	var exists models.MiniApp
	if err := e.DB.Where("id = ?", id).Take(&exists).Error; err == nil {
		Abort(c, http.StatusConflict, "小程序 ID 已被占用，请换一个")
		return
	}

	pkg, err := c.FormFile("package")
	if err != nil {
		Abort(c, http.StatusBadRequest, "请上传小程序包（.zip 或 .nxpkg）")
		return
	}
	if ext := strings.ToLower(filepath.Ext(pkg.Filename)); ext != ".zip" && ext != ".nxpkg" {
		Abort(c, http.StatusBadRequest, "安装包必须是 .zip 或 .nxpkg 文件")
		return
	}
	if pkg.Size <= 0 || pkg.Size > maxPackageSize {
		Abort(c, http.StatusBadRequest, "安装包大小需在 32MB 以内")
		return
	}

	var icon *struct{ Size int64; Filename string }
	iconFile, iconErr := c.FormFile("icon")
	if iconErr == nil {
		ext := strings.ToLower(filepath.Ext(iconFile.Filename))
		if !IsImageExt(ext) {
			Abort(c, http.StatusBadRequest, "图标必须是 png/jpg/webp")
			return
		}
		if iconFile.Size <= 0 || iconFile.Size > maxIconSize {
			Abort(c, http.StatusBadRequest, "图标大小需在 2MB 以内")
			return
		}
		icon = &struct{ Size int64; Filename string }{iconFile.Size, iconFile.Filename}
	}

	// 解包到数据目录（ExtractPackage 自身会校验路径与 app.json）。
	tmp, err := os.CreateTemp("", "nxpkg-submit-*")
	if err != nil {
		Abort(c, http.StatusInternalServerError, "安装包保存失败")
		return
	}
	tmpPath := tmp.Name()
	_ = tmp.Close()
	defer os.Remove(tmpPath)
	if err := c.SaveUploadedFile(pkg, tmpPath); err != nil {
		Abort(c, http.StatusInternalServerError, "安装包保存失败")
		return
	}
	if err := miniappstore.ExtractPackage(tmpPath, miniappstore.PackageDir(e.Cfg.DataDir, id)); err != nil {
		Abort(c, http.StatusBadRequest, err.Error())
		return
	}

	iconName := ""
	if icon != nil {
		iconName = "icon" + strings.ToLower(filepath.Ext(icon.Filename))
		if err := c.SaveUploadedFile(iconFile, filepath.Join(miniappstore.MiniAppDir(e.Cfg.DataDir, id), iconName)); err != nil {
			_ = os.RemoveAll(miniappstore.MiniAppDir(e.Cfg.DataDir, id))
			Abort(c, http.StatusInternalServerError, "图标保存失败")
			return
		}
	}

	version := strings.TrimSpace(c.PostForm("version"))
	if version == "" {
		version = "1.0.0"
	}
	category := strings.TrimSpace(c.PostForm("category"))
	if category == "" {
		category = "其他"
	}
	now := NowMS()
	// 用 map 显式插入：Published 带 default:true 标签，struct 插入时
	// GORM 会省略零值 false 让数据库默认值生效，导致投稿直接上架。
	record := map[string]interface{}{
		"id":          id,
		"title":       title,
		"summary":     strings.TrimSpace(c.PostForm("summary")),
		"description": strings.TrimSpace(c.PostForm("description")),
		"category":    category,
		"developer":   strings.TrimSpace(c.PostForm("developer")),
		"version":     version,
		"icon":        iconName,
		"published":   false, // 投稿默认待审核，管理员上架后公开
		"downloads":   0,
		"created_at":  now,
		"updated_at":  now,
	}
	if err := e.DB.Model(&models.MiniApp{}).Create(record).Error; err != nil {
		_ = os.RemoveAll(miniappstore.MiniAppDir(e.Cfg.DataDir, id))
		Abort(c, http.StatusInternalServerError, "投稿失败，请稍后再试")
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"id":      id,
		"title":   title,
		"message": "投稿成功，等待管理员审核上架",
	})
}
