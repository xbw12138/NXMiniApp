package api

import (
	"fmt"
	"net/http"
	"path/filepath"
	"strconv"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

	"nxminiapp/server/internal/models"
)

// StoreApp 的下载来源。
const (
	SourceUpload    = "upload"
	SourceContainer = "container"
)

// LatestContainerRelease 返回最新的已发布容器版本（列表首个即最新）。
func LatestContainerRelease(db *gorm.DB) *models.ContainerRelease {
	var rel models.ContainerRelease
	if err := db.Where("published = ?", true).Order("created_at DESC, id DESC").Take(&rel).Error; err != nil {
		return nil
	}
	return &rel
}

// ReleaseJSON 是容器版本对外/对后台统一的 JSON 表示。
func ReleaseJSON(r *models.ContainerRelease) gin.H {
	return gin.H{
		"id":           r.ID,
		"version":      r.Version,
		"notes":        r.Notes,
		"size":         r.Size,
		"sha256":       r.SHA256,
		"downloads":    r.Downloads,
		"published":    r.Published,
		"created_at":   r.CreatedAt,
		"download_url": fmt.Sprintf("/api/v1/container/releases/%d/download", r.ID),
	}
}

// ListContainerReleases 公开列表：仅已发布版本，按发布时间倒序（首个即最新版）。
func (e *Env) ListContainerReleases(c *gin.Context) {
	rows := make([]models.ContainerRelease, 0)
	e.DB.Where("published = ?", true).Order("created_at DESC, id DESC").Find(&rows)
	items := make([]gin.H, 0, len(rows))
	for i := range rows {
		items = append(items, ReleaseJSON(&rows[i]))
	}
	c.JSON(http.StatusOK, gin.H{"items": items, "updated_at": NowMS()})
}

// DownloadContainerRelease 下载指定已发布版本并累计下载次数。
func (e *Env) DownloadContainerRelease(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Abort(c, http.StatusNotFound, "版本不存在")
		return
	}
	var r models.ContainerRelease
	if err := e.DB.Where("id = ? AND published = ?", id, true).Take(&r).Error; err != nil {
		Abort(c, http.StatusNotFound, "版本不存在")
		return
	}
	path := filepath.Join(e.Cfg.ReleasesDir(), r.Filename)
	if !FileExists(path) {
		Abort(c, http.StatusGone, "安装包文件丢失")
		return
	}
	e.DB.Model(&models.ContainerRelease{}).Where("id = ?", r.ID).
		Update("downloads", gorm.Expr("downloads + 1"))

	c.Header("X-Version", r.Version)
	c.Header("X-SHA256", r.SHA256)
	c.Header("X-File-Size", strconv.FormatInt(r.Size, 10))
	c.Header("Cache-Control", "no-store")
	c.Header("Content-Type", "application/octet-stream")
	c.Header("Content-Disposition",
		fmt.Sprintf("attachment; filename=\"NXMiniApp-%s.nro\"", SlugFor(r.Version)))
	c.File(path)
}
