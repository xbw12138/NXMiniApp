package api

import (
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
)

// 单次上报的日志上限。容器日志通常只有几十 KB，留出余量防止刷爆磁盘。
const maxDeviceLogSize = 8 << 20

// 设备 ID 只允许十六进制/连字符，避免路径穿越。
var deviceIDPattern = regexp.MustCompile(`^[A-Za-z0-9_-]{4,64}$`)

// UploadDeviceLog 接收 Switch 端 Updater 上传的容器日志。
// multipart: file=<日志文件>，可选 version=（容器版本），设备标识取 X-Device-Id。
func (e *Env) UploadDeviceLog(c *gin.Context) {
	deviceID := strings.TrimSpace(c.GetHeader("X-Device-Id"))
	if !deviceIDPattern.MatchString(deviceID) {
		Abort(c, http.StatusBadRequest, "设备标识无效")
		return
	}
	file, err := c.FormFile("file")
	if err != nil {
		Abort(c, http.StatusBadRequest, "请选择日志文件")
		return
	}
	if file.Size <= 0 || file.Size > maxDeviceLogSize {
		Abort(c, http.StatusBadRequest, "日志大小需在 8MB 以内")
		return
	}
	version := strings.TrimSpace(c.PostForm("version"))
	if len(version) > 32 {
		version = version[:32]
	}
	if version == "" {
		version = "unknown"
	}

	dir := filepath.Join(e.Cfg.DeviceLogsDir(), deviceID)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		Abort(c, http.StatusInternalServerError, "日志目录创建失败")
		return
	}
	stamp := time.Now().Format("20060102-150405")
	name := fmt.Sprintf("%s-%s.log", stamp, SlugFor(version))
	dst := filepath.Join(dir, name)
	if err := c.SaveUploadedFile(file, dst); err != nil {
		Abort(c, http.StatusInternalServerError, "日志保存失败")
		return
	}
	c.JSON(http.StatusOK, gin.H{
		"ok":       true,
		"id":       deviceID + "/" + name,
		"size":     file.Size,
		"received": NowMS(),
	})
}
