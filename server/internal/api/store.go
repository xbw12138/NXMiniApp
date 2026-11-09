package api

import (
	"fmt"
	"net/http"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

	"nxminiapp/server/internal/models"
)

// GetApp loads an app row, aborting with the FastAPI error semantics when it
// is missing or unpublished. Returns nil after aborting.
func (e *Env) GetApp(c *gin.Context, id string, publishedOnly bool) *models.StoreApp {
	var app models.StoreApp
	if err := e.DB.Where("id = ?", id).Take(&app).Error; err != nil {
		Abort(c, http.StatusNotFound, "应用不存在")
		return nil
	}
	if publishedOnly && !app.Published {
		Abort(c, http.StatusNotFound, "应用未发布")
		return nil
	}
	return &app
}

func (e *Env) Health(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{"status": "ok", "version": AppVersion})
}

type categoryRow struct {
	Category string
	Count    int64
}

// Categories lists published categories with their app counts.
func (e *Env) Categories(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{"items": e.categoryItems()})
}

func (e *Env) categoryItems() []gin.H {
	rows := make([]categoryRow, 0)
	e.DB.Model(&models.StoreApp{}).
		Select("category, COUNT(*) AS count").
		Where("published = ?", true).
		Group("category").
		Order("count DESC, category ASC").
		Scan(&rows)
	items := make([]gin.H, 0, len(rows))
	for _, row := range rows {
		items = append(items, gin.H{"id": row.Category, "name": row.Category, "count": row.Count})
	}
	return items
}

func (e *Env) appListJSON(query *gorm.DB) []gin.H {
	apps := make([]models.StoreApp, 0)
	query.Find(&apps)
	items := make([]gin.H, 0, len(apps))
	for i := range apps {
		items = append(items, AppJSON(e.DB, &apps[i], false))
	}
	return items
}

func (e *Env) Home(c *gin.Context) {
	featured := e.appListJSON(e.DB.Where("published = ? AND featured = ?", true, true).
		Order("downloads DESC, updated_at DESC").Limit(12))
	newest := e.appListJSON(e.DB.Where("published = ?", true).
		Order("updated_at DESC").Limit(12))
	popular := e.appListJSON(e.DB.Where("published = ?", true).
		Order("downloads DESC, updated_at DESC").Limit(12))
	c.JSON(http.StatusOK, gin.H{
		"featured":   featured,
		"newest":     newest,
		"popular":    popular,
		"categories": e.categoryItems(),
	})
}

func (e *Env) ListApps(c *gin.Context) {
	q := strings.TrimSpace(c.Query("q"))
	category := strings.TrimSpace(c.Query("category"))
	page := PositiveInt(c.Query("page"), 1)
	pageSize := PositiveInt(c.Query("page_size"), 24)
	if pageSize > 100 {
		pageSize = 100
	}

	query := e.DB.Model(&models.StoreApp{}).Where("published = ?", true)
	if q != "" {
		needle := "%" + q + "%"
		query = query.Where("title LIKE ? OR summary LIKE ? OR developer LIKE ?", needle, needle, needle)
	}
	if category != "" {
		query = query.Where("category = ?", category)
	}
	if raw := c.Query("featured"); raw != "" {
		if value, err := strconv.ParseBool(raw); err == nil {
			query = query.Where("featured = ?", value)
		}
	}

	var total int64
	query.Count(&total)

	order := "updated_at DESC"
	switch c.Query("sort") {
	case "popular":
		order = "downloads DESC, updated_at DESC"
	case "name":
		order = "title ASC"
	}

	apps := make([]models.StoreApp, 0)
	query.Order(order).Limit(pageSize).Offset((page - 1) * pageSize).Find(&apps)
	items := make([]gin.H, 0, len(apps))
	for i := range apps {
		items = append(items, AppJSON(e.DB, &apps[i], false))
	}
	c.JSON(http.StatusOK, gin.H{
		"items":     items,
		"total":     total,
		"page":      page,
		"page_size": pageSize,
	})
}

func (e *Env) AppDetail(c *gin.Context) {
	app := e.GetApp(c, c.Param("id"), true)
	if app == nil {
		return
	}
	payload := AppJSON(e.DB, app, false)
	versions := make([]models.Version, 0)
	e.DB.Where("app_id = ?", app.ID).Order("created_at DESC").Find(&versions)
	items := make([]gin.H, 0, len(versions))
	for i := range versions {
		items = append(items, VersionJSON(app.ID, &versions[i]))
	}
	payload["versions"] = items
	c.JSON(http.StatusOK, payload)
}

func (e *Env) DownloadApp(c *gin.Context) {
	id := c.Param("id")
	app := e.GetApp(c, id, true)
	if app == nil {
		return
	}

	// 跟随容器版本的应用：直接下发最新的官方容器 NRO。
	if app.Source == SourceContainer {
		rel := LatestContainerRelease(e.DB)
		if rel == nil {
			Abort(c, http.StatusNotFound, "容器版本不存在")
			return
		}
		path := filepath.Join(e.Cfg.ReleasesDir(), rel.Filename)
		if !FileExists(path) {
			Abort(c, http.StatusGone, "安装包文件丢失")
			return
		}
		e.DB.Model(&models.StoreApp{}).Where("id = ?", id).Update("downloads", gorm.Expr("downloads + 1"))
		e.DB.Model(&models.ContainerRelease{}).Where("id = ?", rel.ID).
			Update("downloads", gorm.Expr("downloads + 1"))

		c.Header("X-Version", rel.Version)
		c.Header("X-SHA256", rel.SHA256)
		c.Header("X-File-Size", strconv.FormatInt(rel.Size, 10))
		c.Header("Cache-Control", "no-store")
		c.Header("Content-Type", "application/octet-stream")
		c.Header("Content-Disposition",
			fmt.Sprintf("attachment; filename=\"%s-%s.nro\"", id, SlugFor(rel.Version)))
		c.File(path)
		return
	}

	var target *models.Version
	if c.DefaultQuery("version", "latest") == "latest" {
		target = LatestVersion(e.DB, id)
	} else {
		var v models.Version
		if err := e.DB.Where("app_id = ? AND version = ?", id, c.Query("version")).Take(&v).Error; err == nil {
			target = &v
		}
	}
	if target == nil {
		Abort(c, http.StatusNotFound, "版本不存在")
		return
	}

	path := filepath.Join(e.Cfg.FilesDir(), target.Filename)
	if !FileExists(path) {
		Abort(c, http.StatusGone, "安装包文件丢失")
		return
	}

	e.DB.Model(&models.StoreApp{}).Where("id = ?", id).Update("downloads", gorm.Expr("downloads + 1"))

	c.Header("X-Version", target.Version)
	c.Header("X-SHA256", target.SHA256)
	c.Header("X-File-Size", strconv.FormatInt(target.Size, 10))
	c.Header("Cache-Control", "no-store")
	c.Header("Content-Type", "application/octet-stream")
	c.Header("Content-Disposition",
		fmt.Sprintf("attachment; filename=\"%s-%s.nro\"", id, SlugFor(target.Version)))
	c.File(path)
}

type checkUpdateItem struct {
	ID      string `json:"id"`
	Version string `json:"version"`
}

func (e *Env) CheckUpdates(c *gin.Context) {
	var body struct {
		Items []checkUpdateItem `json:"items"`
	}
	if err := c.ShouldBindJSON(&body); err != nil {
		Abort(c, http.StatusBadRequest, "请求体无效")
		return
	}
	updates := make([]gin.H, 0)
	for _, item := range body.Items {
		var app models.StoreApp
		if err := e.DB.Where("id = ? AND published = ?", item.ID, true).Take(&app).Error; err != nil {
			continue
		}
		latest := LatestVersion(e.DB, item.ID)
		if latest == nil {
			continue
		}
		if latest.Version != item.Version {
			updates = append(updates, AppJSON(e.DB, &app, false))
		}
	}
	c.JSON(http.StatusOK, gin.H{"updates": updates})
}
