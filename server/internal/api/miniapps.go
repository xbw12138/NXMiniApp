package api

import (
	"bytes"
	"net/http"
	"path/filepath"
	"strings"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

	"nxminiapp/server/internal/config"
	"nxminiapp/server/internal/miniappstore"
	"nxminiapp/server/internal/models"
)

// MiniAppJSON serializes a mini program row to the public contract; size is
// computed live from the on-disk package directory.
func MiniAppJSON(cfg config.Config, m *models.MiniApp) gin.H {
	iconURL := ""
	if m.Icon != "" && FileExists(filepath.Join(miniappstore.MiniAppDir(cfg.DataDir, m.ID), m.Icon)) {
		iconURL = "/api/v1/miniapps/" + m.ID + "/icon"
	}
	category := m.Category
	if category == "" {
		category = "其他"
	}
	version := m.Version
	if version == "" {
		version = "1.0.0"
	}
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
		"published":    m.Published,
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

	query := e.DB.Where("published = ?", true)
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
		if !DirExists(miniappstore.PackageDir(e.Cfg.DataDir, rows[i].ID)) {
			continue
		}
		items = append(items, MiniAppJSON(e.Cfg, &rows[i]))
	}
	c.JSON(http.StatusOK, gin.H{"items": items, "updated_at": NowMS()})
}

func (e *Env) MiniAppDetail(c *gin.Context) {
	m := e.GetMiniApp(c, c.Param("id"))
	if m == nil {
		return
	}
	if !DirExists(miniappstore.PackageDir(e.Cfg.DataDir, m.ID)) {
		Abort(c, http.StatusNotFound, "package missing")
		return
	}
	c.JSON(http.StatusOK, MiniAppJSON(e.Cfg, m))
}

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
	c.File(path)
}

func (e *Env) DownloadMiniApp(c *gin.Context) {
	m := e.GetMiniApp(c, c.Param("id"))
	if m == nil {
		return
	}
	pkgDir := miniappstore.PackageDir(e.Cfg.DataDir, m.ID)
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

	version := m.Version
	if version == "" {
		version = "1.0.0"
	}
	c.Header("Content-Disposition",
		"attachment; filename=\""+m.ID+"-"+version+".nxpkg\"")
	c.Data(http.StatusOK, "application/zip", buf.Bytes())
}
