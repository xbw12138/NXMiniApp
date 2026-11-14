package api

import (
	"bytes"
	"net/http"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

	"nxminiapp/server/internal/config"
	"nxminiapp/server/internal/miniappstore"
	"nxminiapp/server/internal/models"
)

// MiniAppJSON serializes a mini program row to the public contract; size is
// computed live from the on-disk package directory.
func MiniAppJSON(cfg config.Config, m *models.MiniApp) gin.H {
	return MiniAppJSONDevice(cfg, m, "")
}

// MiniAppJSONDevice 按设备返回有效版本（灰度命中时返回灰度版本号）。
func MiniAppJSONDevice(cfg config.Config, m *models.MiniApp, device string) gin.H {
	iconURL := ""
	if m.Icon != "" && FileExists(filepath.Join(miniappstore.MiniAppDir(cfg.DataDir, m.ID), m.Icon)) {
		iconURL = "/api/v1/miniapps/" + m.ID + "/icon"
		// 带上更新时间，后台换图标后 URL 变化，客户端/浏览器才会重新拉取。
		if m.UpdatedAt > 0 {
			iconURL += "?v=" + strconv.FormatInt(m.UpdatedAt, 10)
		}
	}
	category := m.Category
	if category == "" {
		category = "其他"
	}
	version := effectiveVersion(m, device)
	return gin.H{
		"id":           m.ID,
		"title":        m.Title,
		"summary":      m.Summary,
		"description":  m.Description,
		"category":     category,
		"developer":    m.Developer,
		"icon_url":     iconURL,
		"version":      version,
		"size":         miniappstore.DirSize(miniappstore.PackageDir(cfg.DataDir, m.ID)),
		"featured":     m.Featured,
		"sort":         m.Sort,
		"published":    m.Published,
		"downloads":    m.Downloads,
		"opens":        m.Opens,
		"uv":           m.UV,
		"updated_at":   m.UpdatedAt,
		"download_url": "/api/v1/miniapps/" + m.ID + "/download",
	}
}

// GetMiniApp loads a mini program row with the FastAPI 404 semantics.
func (e *Env) GetMiniApp(c *gin.Context, id string) *models.MiniApp {
	if !IDPattern.MatchString(id) {
		Abort(c, http.StatusNotFound, "mini app not found")
		return nil
	}
	var m models.MiniApp
	if err := e.DB.Where("id = ?", id).Take(&m).Error; err != nil {
		Abort(c, http.StatusNotFound, "mini app not found")
		return nil
	}
	return &m
}

func (e *Env) ListMiniApps(c *gin.Context) {
	q := strings.TrimSpace(c.Query("q"))
	category := strings.TrimSpace(c.Query("category"))
	device := deviceID(c)
	page := 1
	pageSize := 0
	if v, err := strconv.Atoi(c.Query("page")); err == nil && v > 0 {
		page = v
	}
	if v, err := strconv.Atoi(c.Query("page_size")); err == nil && v > 0 {
		pageSize = v
	}

	query := e.DB.Model(&models.MiniApp{})
	// 公开商店只展示已通过审核的小程序；published 只控制上下线。
	query = query.Where("review_status = ? OR review_status = ''", models.ReviewApproved)
	if category != "" {
		query = query.Where("category = ?", category)
	}
	if q != "" {
		needle := "%" + q + "%"
		query = query.Where("title LIKE ? OR summary LIKE ? OR id LIKE ?", needle, needle, needle)
	}
	rows := make([]models.MiniApp, 0)
	query.Order("id ASC").Find(&rows)

	items := make([]gin.H, 0, len(rows))
	for i := range rows {
		if !visibleTo(&rows[i], device) {
			continue
		}
		if !DirExists(miniappstore.PackageDir(e.Cfg.DataDir, rows[i].ID)) {
			continue
		}
		items = append(items, MiniAppJSONDevice(e.Cfg, &rows[i], device))
	}
	resp := gin.H{"items": items, "updated_at": NowMS()}
	// 带 page_size 时走分页返回；不带则保持旧行为（全量），兼容老客户端。
	if pageSize > 0 {
		total := len(items)
		totalPages := (total + pageSize - 1) / pageSize
		if totalPages < 1 {
			totalPages = 1
		}
		start := (page - 1) * pageSize
		if start > total {
			start = total
		}
		end := start + pageSize
		if end > total {
			end = total
		}
		resp["items"] = items[start:end]
		resp["total"] = total
		resp["page"] = page
		resp["page_size"] = pageSize
		resp["total_pages"] = totalPages
		resp["has_more"] = end < total
	}
	c.JSON(http.StatusOK, resp)
}

func (e *Env) MiniAppDetail(c *gin.Context) {
	m := e.GetMiniApp(c, c.Param("id"))
	if m == nil {
		return
	}
	if !visibleTo(m, deviceID(c)) {
		Abort(c, http.StatusNotFound, "mini app not found")
		return
	}
	if !DirExists(miniappstore.PackageDir(e.Cfg.DataDir, m.ID)) {
		Abort(c, http.StatusNotFound, "package missing")
		return
	}
	c.JSON(http.StatusOK, MiniAppJSONDevice(e.Cfg, m, deviceID(c)))
}

// MiniAppIcon 返回小程序图标；图标会被就地替换，禁止中间层长期缓存。
func (e *Env) MiniAppIcon(c *gin.Context) {
	m := e.GetMiniApp(c, c.Param("id"))
	if m == nil {
		return
	}
	if m.Icon == "" {
		Abort(c, http.StatusNotFound, "icon not found")
		return
	}
	path := filepath.Join(miniappstore.MiniAppDir(e.Cfg.DataDir, m.ID), m.Icon)
	if !FileExists(path) {
		Abort(c, http.StatusNotFound, "icon not found")
		return
	}
	c.Header("Cache-Control", "no-cache")
	c.File(path)
}

func (e *Env) DownloadMiniApp(c *gin.Context) {
	m := e.GetMiniApp(c, c.Param("id"))
	if m == nil {
		return
	}
	// 未通过审核的小程序不对外开放下载。
	if !IsApproved(m) {
		Abort(c, http.StatusNotFound, "mini app not found")
		return
	}
	device := deviceID(c)
	version := effectiveVersion(m, device)

	pkgDir := miniappstore.PackageDir(e.Cfg.DataDir, m.ID)
	grayDir := miniappstore.GrayPackageDir(e.Cfg.DataDir, m.ID)
	// 命中灰度且灰度包已上传时，下发灰度包。
	if strings.TrimSpace(m.GrayVersion) != "" && version == strings.TrimSpace(m.GrayVersion) &&
		DirExists(grayDir) {
		pkgDir = grayDir
	}
	if !DirExists(pkgDir) {
		Abort(c, http.StatusNotFound, "package missing")
		return
	}

	var buf bytes.Buffer
	if err := miniappstore.WriteStoredZip(&buf, pkgDir); err != nil {
		Abort(c, http.StatusInternalServerError, "打包失败")
		return
	}

	e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Update("downloads", gorm.Expr("downloads + 1"))

	c.Header("Content-Disposition",
		"attachment; filename=\""+m.ID+"-"+version+".nxpkg\"")
	c.Data(http.StatusOK, "application/zip", buf.Bytes())
}

// OpenMiniApp 上报一次小程序启动。body 可带 {"device_id":"..."}。
// opens (PV) + 1；若该设备首次打开此小程序，uv + 1。
func (e *Env) OpenMiniApp(c *gin.Context) {
	m := e.GetMiniApp(c, c.Param("id"))
	if m == nil {
		return
	}
	var body struct {
		DeviceID string `json:"device_id"`
	}
	_ = c.ShouldBindJSON(&body)

	e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Update("opens", gorm.Expr("opens + 1"))

	if body.DeviceID != "" {
		open := models.MiniAppOpen{MiniAppID: m.ID, DeviceID: body.DeviceID, CreatedAt: time.Now().UnixMilli()}
		result := e.DB.Create(&open)
		if result.RowsAffected > 0 {
			e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Update("uv", gorm.Expr("uv + 1"))
		}
	}
	c.JSON(http.StatusOK, gin.H{"ok": true})
}
