package api

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

	"nxminiapp/server/internal/config"
	"nxminiapp/server/internal/miniappstore"
	"nxminiapp/server/internal/models"
)

// 上传者（开发者）身份：投稿时签发一次性明文密钥，服务端只保存 sha256。
// 之后用 Authorization: Bearer <key> 或 HttpOnly Cookie 维护自己的小程序。
const (
	devKeyPrefix  = "nxdev_"
	devCookieName = "nxdev_key"
	devCookieAge  = 30 * 24 * 3600
	devCtxKey     = "nxdev_owner_hash"
)

func newDevKey() string {
	return devKeyPrefix + RandomHex(24)
}

// NewDevKey 生成一个新的开发者密钥（明文只回显一次）。
func NewDevKey() string { return newDevKey() }

func hashDevKey(key string) string {
	sum := sha256.Sum256([]byte(strings.TrimSpace(key)))
	return hex.EncodeToString(sum[:])
}

// HashDevKey 计算开发者密钥的存储哈希。
func HashDevKey(key string) string { return hashDevKey(key) }

// devKeyFromRequest 读取开发者密钥（请求头优先，其次 Cookie）。
func devKeyFromRequest(c *gin.Context) (string, bool) {
	header := c.GetHeader("Authorization")
	if len(header) > 7 && strings.EqualFold(header[:7], "bearer ") {
		key := strings.TrimSpace(header[7:])
		if strings.HasPrefix(key, devKeyPrefix) {
			return key, true
		}
	}
	if cookie, err := c.Cookie(devCookieName); err == nil {
		if key := strings.TrimSpace(cookie); strings.HasPrefix(key, devKeyPrefix) {
			return key, true
		}
	}
	return "", false
}

// DevAuth 是上传者接口的鉴权中间件：把 owner_key_hash 放进上下文。
func (e *Env) DevAuth() gin.HandlerFunc {
	return func(c *gin.Context) {
		key, ok := devKeyFromRequest(c)
		if !ok {
			Abort(c, http.StatusUnauthorized, "请先使用开发者密钥登录")
			return
		}
		c.Set(devCtxKey, hashDevKey(key))
		c.Next()
	}
}

// DevSession 用开发者密钥换取 HttpOnly Cookie，方便门户页刷新后免输入。
func (e *Env) DevSession(c *gin.Context) {
	var body struct {
		Key string `json:"key"`
	}
	if err := c.ShouldBindJSON(&body); err != nil {
		Abort(c, http.StatusBadRequest, "请求体无效")
		return
	}
	key := strings.TrimSpace(body.Key)
	if !strings.HasPrefix(key, devKeyPrefix) || len(key) < len(devKeyPrefix)+16 {
		Abort(c, http.StatusUnauthorized, "开发者密钥格式不正确")
		return
	}
	hash := hashDevKey(key)
	var count int64
	if err := e.DB.Model(&models.MiniApp{}).Where("owner_key_hash = ?", hash).Count(&count).Error; err != nil {
		Abort(c, http.StatusInternalServerError, "登录失败，请稍后再试")
		return
	}
	if count == 0 {
		Abort(c, http.StatusUnauthorized, "开发者密钥无效")
		return
	}
	c.SetSameSite(http.SameSiteLaxMode)
	c.SetCookie(devCookieName, key, devCookieAge, "/", "", false, true)
	c.JSON(http.StatusOK, gin.H{"ok": true, "count": count})
}

// DevLogout 清除开发者 Cookie。
func (e *Env) DevLogout(c *gin.Context) {
	c.SetSameSite(http.SameSiteLaxMode)
	c.SetCookie(devCookieName, "", -1, "/", "", false, true)
	c.JSON(http.StatusOK, gin.H{"ok": true})
}

// ReviewStatusOf 兼容旧数据：空值视为已通过。
func ReviewStatusOf(m *models.MiniApp) string {
	if strings.TrimSpace(m.ReviewStatus) == "" {
		return models.ReviewApproved
	}
	return m.ReviewStatus
}

// IsApproved 表示小程序已通过审核（是否上线另看 Published）。
func IsApproved(m *models.MiniApp) bool {
	return ReviewStatusOf(m) == models.ReviewApproved
}

// HasPending 表示存在等待审核的新版本。
func HasPending(m *models.MiniApp) bool {
	return strings.TrimSpace(m.PendingVersion) != "" || strings.TrimSpace(m.PendingKind) != ""
}

// PendingJSON 返回待审核版本信息；没有待审内容时返回 nil。
func PendingJSON(m *models.MiniApp) any {
	if !HasPending(m) {
		return nil
	}
	kind := strings.TrimSpace(m.PendingKind)
	if kind == "" {
		kind = models.PendingStable
	}
	return gin.H{
		"kind":         kind,
		"version":      m.PendingVersion,
		"size":         m.PendingSize,
		"submitted_at": m.PendingSubmittedAt,
	}
}

// devMiniAppJSON 是上传者门户看到的小程序信息。
func devMiniAppJSON(e *Env, m *models.MiniApp) gin.H {
	item := MiniAppJSON(e.Cfg, m)
	for key, value := range RolloutJSON(m) {
		item[key] = value
	}
	item["review_status"] = ReviewStatusOf(m)
	item["review_note"] = m.ReviewNote
	item["created_at"] = m.CreatedAt
	item["owner_bound"] = m.OwnerKeyHash != ""
	item["pending"] = PendingJSON(m)
	return item
}

// ownedMiniApp 按 ID 读取并校验归属，越权一律返回 404。
func (e *Env) ownedMiniApp(c *gin.Context) *models.MiniApp {
	hash, _ := c.Get(devCtxKey)
	m := e.GetMiniApp(c, c.Param("id"))
	if m == nil {
		return nil
	}
	if m.OwnerKeyHash == "" || m.OwnerKeyHash != hash.(string) {
		Abort(c, http.StatusNotFound, "小程序不存在")
		return nil
	}
	return m
}

// DevListMiniApps 列出当前开发者投稿的小程序。
func (e *Env) DevListMiniApps(c *gin.Context) {
	hash, _ := c.Get(devCtxKey)
	rows := make([]models.MiniApp, 0)
	e.DB.Where("owner_key_hash = ?", hash).Order("updated_at DESC").Find(&rows)
	items := make([]gin.H, 0, len(rows))
	for i := range rows {
		items = append(items, devMiniAppJSON(e, &rows[i]))
	}
	c.JSON(http.StatusOK, gin.H{"items": items})
}

// DevUploadPackage 上传新版本：先进入待审核目录，审核通过后才替换线上包。
func (e *Env) DevUploadPackage(c *gin.Context) {
	m := e.ownedMiniApp(c)
	if m == nil {
		return
	}
	file, err := c.FormFile("file")
	if err != nil {
		Abort(c, http.StatusBadRequest, "请选择小程序包")
		return
	}
	ext := strings.ToLower(filepath.Ext(file.Filename))
	if ext != ".nxpkg" && ext != ".zip" {
		Abort(c, http.StatusBadRequest, "安装包必须是 .nxpkg 或 .zip 文件")
		return
	}
	if file.Size <= 0 || file.Size > maxPackageSize {
		Abort(c, http.StatusBadRequest, "安装包大小需在 32MB 以内")
		return
	}
	version := strings.TrimSpace(c.PostForm("version"))
	if version == "" {
		Abort(c, http.StatusBadRequest, "请填写版本号")
		return
	}
	gray := c.PostForm("gray") == "1" || c.PostForm("gray") == "true"
	if gray && !IsApproved(m) {
		Abort(c, http.StatusBadRequest, "首次投稿请上传稳定版本，通过审核后才能上传灰度版本")
		return
	}

	tmp, err := os.CreateTemp("", "nxpkg-dev-*")
	if err != nil {
		Abort(c, http.StatusInternalServerError, "安装包保存失败")
		return
	}
	tmpPath := tmp.Name()
	_ = tmp.Close()
	defer os.Remove(tmpPath)
	if err := c.SaveUploadedFile(file, tmpPath); err != nil {
		Abort(c, http.StatusInternalServerError, "安装包保存失败")
		return
	}

	kind := models.PendingStable
	dest := miniappstore.PendingPackageDir(e.Cfg.DataDir, m.ID)
	other := miniappstore.PendingGrayPackageDir(e.Cfg.DataDir, m.ID)
	if gray {
		kind = models.PendingGray
		dest = miniappstore.PendingGrayPackageDir(e.Cfg.DataDir, m.ID)
		other = miniappstore.PendingPackageDir(e.Cfg.DataDir, m.ID)
	}
	if err := miniappstore.ExtractPackage(tmpPath, dest); err != nil {
		Abort(c, http.StatusBadRequest, err.Error())
		return
	}
	_ = os.RemoveAll(other)

	updates := map[string]any{
		"pending_kind":         kind,
		"pending_version":      version,
		"pending_size":         miniappstore.DirSize(dest),
		"pending_percent":      ClampPercent(c.PostForm("percent")),
		"pending_whitelist":    NormalizeDevices(c.PostForm("whitelist")),
		"pending_submitted_at": NowMS(),
		"review_note":          "",
		"updated_at":           NowMS(),
	}
	// 首次投稿或被驳回后重新提交：回到待审核状态；已上线的版本保持不变。
	if !IsApproved(m) {
		updates["review_status"] = models.ReviewPending
	}
	e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(updates)
	refetchDevMiniApp(c, e, m.ID)
}

// DevUpdateRollout 调整可见比例与灰度放量。
func (e *Env) DevUpdateRollout(c *gin.Context) {
	m := e.ownedMiniApp(c)
	if m == nil {
		return
	}
	if !IsApproved(m) {
		Abort(c, http.StatusBadRequest, "小程序尚未通过审核，暂不能调整放量")
		return
	}
	var body struct {
		VisiblePercent   *int    `json:"visible_percent"`
		VisibleWhitelist *string `json:"visible_whitelist"`
		GrayPercent      *int    `json:"gray_percent"`
		GrayWhitelist    *string `json:"gray_whitelist"`
	}
	if err := c.ShouldBindJSON(&body); err != nil {
		Abort(c, http.StatusBadRequest, "参数错误")
		return
	}
	updates := map[string]any{"updated_at": NowMS()}
	if body.VisiblePercent != nil {
		updates["visible_percent"] = clampPercentInt(*body.VisiblePercent)
	}
	if body.VisibleWhitelist != nil {
		updates["visible_whitelist"] = NormalizeDevices(*body.VisibleWhitelist)
	}
	if body.GrayPercent != nil {
		updates["gray_percent"] = clampPercentInt(*body.GrayPercent)
	}
	if body.GrayWhitelist != nil {
		updates["gray_whitelist"] = NormalizeDevices(*body.GrayWhitelist)
	}
	e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(updates)
	refetchDevMiniApp(c, e, m.ID)
}

// DevPromoteRollout 把已上线的灰度版本转为全量。
func (e *Env) DevPromoteRollout(c *gin.Context) {
	m := e.ownedMiniApp(c)
	if m == nil {
		return
	}
	if strings.TrimSpace(m.GrayVersion) == "" {
		Abort(c, http.StatusBadRequest, "当前没有灰度版本")
		return
	}
	if err := PromoteGray(e.Cfg, e.DB, m); err != nil {
		Abort(c, http.StatusBadRequest, err.Error())
		return
	}
	refetchDevMiniApp(c, e, m.ID)
}

// DevDiscardRollout 撤销灰度，设备回到稳定版本。
func (e *Env) DevDiscardRollout(c *gin.Context) {
	m := e.ownedMiniApp(c)
	if m == nil {
		return
	}
	if err := DiscardGray(e.Cfg, e.DB, m); err != nil {
		Abort(c, http.StatusInternalServerError, err.Error())
		return
	}
	refetchDevMiniApp(c, e, m.ID)
}

// DevSetVisibility 上架或下线。下线不会影响审核状态与统计数据。
func (e *Env) DevSetVisibility(c *gin.Context) {
	m := e.ownedMiniApp(c)
	if m == nil {
		return
	}
	var body struct {
		Published *bool `json:"published"`
	}
	if err := c.ShouldBindJSON(&body); err != nil || body.Published == nil {
		Abort(c, http.StatusBadRequest, "参数错误")
		return
	}
	if !IsApproved(m) {
		Abort(c, http.StatusBadRequest, "小程序尚未通过审核，不能上架")
		return
	}
	if *body.Published && !DirExists(miniappstore.PackageDir(e.Cfg.DataDir, m.ID)) {
		Abort(c, http.StatusBadRequest, "线上安装包不存在，无法上架")
		return
	}
	e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(map[string]any{
		"published":  *body.Published,
		"updated_at": NowMS(),
	})
	refetchDevMiniApp(c, e, m.ID)
}

func refetchDevMiniApp(c *gin.Context, e *Env, id string) {
	var fresh models.MiniApp
	if err := e.DB.Where("id = ?", id).Take(&fresh).Error; err != nil {
		Abort(c, http.StatusNotFound, "小程序不存在")
		return
	}
	c.JSON(http.StatusOK, devMiniAppJSON(e, &fresh))
}

func clampPercentInt(n int) int {
	if n < 0 {
		return 0
	}
	if n > 100 {
		return 100
	}
	return n
}

// PromoteGray 用灰度包替换稳定包，并清空灰度配置。
func PromoteGray(cfg config.Config, db *gorm.DB, m *models.MiniApp) error {
	grayDir := miniappstore.GrayPackageDir(cfg.DataDir, m.ID)
	if !DirExists(grayDir) {
		return errors.New("灰度包不存在")
	}
	stableDir := miniappstore.PackageDir(cfg.DataDir, m.ID)
	_ = os.RemoveAll(stableDir)
	if err := os.Rename(grayDir, stableDir); err != nil {
		return errors.New("全量发布失败")
	}
	return db.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(map[string]any{
		"version":        m.GrayVersion,
		"gray_version":   "",
		"gray_percent":   0,
		"gray_whitelist": "",
		"updated_at":     NowMS(),
	}).Error
}

// DiscardGray 删除灰度包并重置灰度配置。
func DiscardGray(cfg config.Config, db *gorm.DB, m *models.MiniApp) error {
	_ = os.RemoveAll(miniappstore.GrayPackageDir(cfg.DataDir, m.ID))
	return db.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(map[string]any{
		"gray_version":   "",
		"gray_percent":   0,
		"gray_whitelist": "",
		"updated_at":     NowMS(),
	}).Error
}
