package api

import (
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

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
		"views":       item.Views,
		"uv":          item.UV,
		"updated_at":  item.UpdatedAt,
	}
}

// HomeMiniAppJSON 返回首页小程序描述；未配置或包缺失时返回 nil。
// 首页由后台「首页配置」单独指定，不参与商店上架状态：管理员常把首页设为未上架
// 以免它出现在商店列表里，若这里复用 visibleTo 会导致首页版本永远无法下发。
func (e *Env) homeMiniAppJSON() any {
	id := e.SettingValue(SettingHomeMiniApp)
	if id == "" {
		return nil
	}
	var m models.MiniApp
	if err := e.DB.Where("id = ?", id).Take(&m).Error; err != nil {
		return nil
	}
	if !DirExists(miniappstore.PackageDir(e.Cfg.DataDir, m.ID)) {
		return nil
	}
	return MiniAppJSONDevice(e.Cfg, &m, "")
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

	device := deviceID(c)
	featuredRows := make([]models.MiniApp, 0)
	e.DB.Where("featured = ?", true).
		Order("sort ASC, updated_at DESC").Limit(12).Find(&featuredRows)
	featured := make([]gin.H, 0, len(featuredRows))
	for i := range featuredRows {
		if !visibleTo(&featuredRows[i], device) {
			continue
		}
		if !DirExists(miniappstore.PackageDir(e.Cfg.DataDir, featuredRows[i].ID)) {
			continue
		}
		featured = append(featured, MiniAppJSONDevice(e.Cfg, &featuredRows[i], device))
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

// OpenContent 上报一次资讯曝光（点击）。body 可带 {"device_id":"..."}。
// views (PV) + 1；若该设备首次看这条资讯，uv + 1。
func (e *Env) OpenContent(c *gin.Context) {
	id := PositiveInt(c.Param("id"), 0)
	var item models.ContentItem
	if id == 0 || e.DB.Where("id = ? AND published = ?", id, true).Take(&item).Error != nil {
		Abort(c, http.StatusNotFound, "内容不存在")
		return
	}
	var body struct {
		DeviceID string `json:"device_id"`
	}
	_ = c.ShouldBindJSON(&body)

	e.DB.Model(&models.ContentItem{}).Where("id = ?", item.ID).
		Update("views", gorm.Expr("views + 1"))

	if body.DeviceID != "" {
		open := models.ContentOpen{ContentID: item.ID, DeviceID: body.DeviceID, CreatedAt: time.Now().UnixMilli()}
		if result := e.DB.Create(&open); result.RowsAffected > 0 {
			e.DB.Model(&models.ContentItem{}).Where("id = ?", item.ID).
				Update("uv", gorm.Expr("uv + 1"))
		}
	}
	c.JSON(http.StatusOK, gin.H{"ok": true})
}

func contentListJSON(rows []models.ContentItem) []gin.H {
	items := make([]gin.H, 0, len(rows))
	for i := range rows {
		items = append(items, ContentJSON(&rows[i]))
	}
	return items
}
