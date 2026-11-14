package api

import (
	"crypto/sha256"
	"encoding/binary"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"

	"nxminiapp/server/internal/models"
)

// 客户端通过该请求头携带持久化的设备标识，用于灰度分流。
const deviceHeader = "X-Device-Id"

// deviceID 读取设备标识（请求头优先，其次查询参数）。
func deviceID(c *gin.Context) string {
	id := strings.TrimSpace(c.GetHeader(deviceHeader))
	if id == "" {
		id = strings.TrimSpace(c.Query("device_id"))
	}
	if len(id) > 64 {
		id = id[:64]
	}
	return id
}

// bucket 用稳定哈希把设备映射到 0-99 的桶，salt 区分不同灰度维度。
func bucket(device, salt string) int {
	sum := sha256.Sum256([]byte(device + "|" + salt))
	return int(binary.BigEndian.Uint32(sum[:4]) % 100)
}

func inWhitelist(list, device string) bool {
	if device == "" {
		return false
	}
	for _, item := range strings.Split(list, ",") {
		if strings.TrimSpace(item) == device {
			return true
		}
	}
	return false
}

// visibleTo 判断设备能否看到该小程序：先看审核状态，再看白名单与可见比例。
func visibleTo(app *models.MiniApp, device string) bool {
	// 未通过审核的小程序（含待审核、已驳回）对任何设备都不可见。
	if !IsApproved(app) {
		return false
	}
	if inWhitelist(app.VisibleWhitelist, device) {
		return true
	}
	if !app.Published {
		return false
	}
	switch {
	case app.VisiblePercent >= 100:
		return true
	case app.VisiblePercent <= 0:
		return false
	default:
		return bucket(device, app.ID+":visible") < app.VisiblePercent
	}
}

// effectiveVersion 返回该设备应使用的版本：命中灰度规则时取灰度版本。
func effectiveVersion(app *models.MiniApp, device string) string {
	stable := app.Version
	if stable == "" {
		stable = "1.0.0"
	}
	gray := strings.TrimSpace(app.GrayVersion)
	if gray == "" {
		return stable
	}
	if inWhitelist(app.GrayWhitelist, device) {
		return gray
	}
	switch {
	case app.GrayPercent >= 100:
		return gray
	case app.GrayPercent <= 0:
		return stable
	default:
		if bucket(device, app.ID+":gray:"+gray) < app.GrayPercent {
			return gray
		}
		return stable
	}
}

// RolloutJSON 是管理端回显用的灰度配置。
func RolloutJSON(app *models.MiniApp) gin.H {
	return gin.H{
		"visible_percent":   app.VisiblePercent,
		"visible_whitelist": app.VisibleWhitelist,
		"gray_version":      app.GrayVersion,
		"gray_percent":      app.GrayPercent,
		"gray_whitelist":    app.GrayWhitelist,
	}
}

// ClampPercent 把外部输入的百分比限制在 0-100。
func ClampPercent(value string) int {
	n, err := strconv.Atoi(strings.TrimSpace(value))
	if err != nil {
		return 0
	}
	if n < 0 {
		return 0
	}
	if n > 100 {
		return 100
	}
	return n
}

// NormalizeDevices 规整白名单：去掉空白项、限制长度。
func NormalizeDevices(list string) string {
	parts := make([]string, 0, 32)
	for _, item := range strings.Split(list, ",") {
		item = strings.TrimSpace(item)
		if item == "" || len(item) > 64 {
			continue
		}
		parts = append(parts, item)
	}
	result := strings.Join(parts, ",")
	if len(result) > 3900 {
		result = result[:3900]
	}
	return result
}
