package api

import (
	"net/http"

	"github.com/gin-gonic/gin"

	"nxminiapp/server/internal/miniappstore"
	"nxminiapp/server/internal/models"
)

// SettingHomeMiniApp 是保存首页小程序 ID 的配置键。
const SettingHomeMiniApp = "home.miniapp_id"

// SettingValue 读取一个配置项；不存在时返回空字符串。
func (e *Env) SettingValue(key string) string {
	var row models.Setting
	if err := e.DB.Where("`key` = ?", key).Take(&row).Error; err != nil {
		return ""
	}
	return row.Value
}

// ContentJSON 序列化首页内容位（横幅或资讯）。
func ContentJSON(item *models.ContentItem) gin.H {
	coverURL := ""
	if item.Cover != "" {
		coverURL = "/media/covers/" + item.Cover
	}
	return gin.H{
		"id":          item.ID,
		"kind":        item.Kind,
		"title":       item.Title,
		"summary":     item.Summary,
		"body":        item.Body,
		"cover_url":   coverURL,
		"link_type":   item.LinkType,
		"link_target": item.LinkTarget,
		"sort":        item.Sort,
		"published":   item.Published,
		"updated_at":  item.UpdatedAt,
	}
}

// HomeMiniAppJSON 返回首页小程序描述；未配置或包缺失时返回 nil。
func (e *Env) homeMiniAppJSON() any {
	id := e.SettingValue(SettingHomeMiniApp)
	if id == "" {
		return nil
	}
	var m models.MiniApp
	if err := e.DB.Where("id = ? AND published = ?", id, true).Take(&m).Error; err != nil {
		return nil
	}
	if !DirExists(miniappstore.PackageDir(e.Cfg.DataDir, m.ID)) {
		return nil
	}
	return MiniAppJSON(e.Cfg, &m)
}

// MiniAppHome 是容器启动时调用的首页接口：返回首页小程序描述与内容 feed。
// 客户端拿到 home 后按需安装并直接运行该小程序；home 为 null 时回落列表页。
func (e *Env) MiniAppHome(c *gin.Context) {
	banners := make([]models.ContentItem, 0)
	e.DB.Where("kind = ? AND published = ?", "banner", true).
		Order("sort ASC, id ASC").Limit(5).Find(&banners)

	news := make([]models.ContentItem, 0)
	e.DB.Where("kind = ? AND published = ?", "news", true).
		Order("sort ASC, id DESC").Limit(8).Find(&news)

	featuredRows := make([]models.MiniApp, 0)
	e.DB.Where("published = ? AND featured = ?", true, true).
		Order("sort ASC, updated_at DESC").Limit(12).Find(&featuredRows)
	featured := make([]gin.H, 0, len(featuredRows))
	for i := range featuredRows {
		if !DirExists(miniappstore.PackageDir(e.Cfg.DataDir, featuredRows[i].ID)) {
			continue
		}
		featured = append(featured, MiniAppJSON(e.Cfg, &featuredRows[i]))
	}

	c.JSON(http.StatusOK, gin.H{
		"home":       e.homeMiniAppJSON(),
		"banners":    contentListJSON(banners),
		"news":       contentListJSON(news),
		"featured":   featured,
		"updated_at": NowMS(),
	})
}

// ContentDetail 返回单条资讯详情，供首页小程序渲染正文。
func (e *Env) ContentDetail(c *gin.Context) {
	id := PositiveInt(c.Param("id"), 0)
	var item models.ContentItem
	if id == 0 || e.DB.Where("id = ? AND published = ?", id, true).Take(&item).Error != nil {
		Abort(c, http.StatusNotFound, "内容不存在")
		return
	}
	c.JSON(http.StatusOK, ContentJSON(&item))
}

func contentListJSON(rows []models.ContentItem) []gin.H {
	items := make([]gin.H, 0, len(rows))
	for i := range rows {
		items = append(items, ContentJSON(&rows[i]))
	}
	return items
}
