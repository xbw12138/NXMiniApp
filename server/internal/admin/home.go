package admin

import (
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"

	"nxminiapp/server/internal/api"
	"nxminiapp/server/internal/models"
)

func listContent(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		kind := strings.TrimSpace(c.Query("kind"))
		query := e.DB.Model(&models.ContentItem{})
		if kind != "" {
			query = query.Where("kind = ?", kind)
		}
		rows := make([]models.ContentItem, 0)
		query.Order("kind ASC, sort ASC, id DESC").Find(&rows)
		items := make([]gin.H, 0, len(rows))
		for i := range rows {
			items = append(items, api.ContentJSON(&rows[i]))
		}
		c.JSON(http.StatusOK, gin.H{"items": items})
	}
}

func createContent(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			Kind       string `json:"kind"`
			Title      string `json:"title"`
			Summary    string `json:"summary"`
			Body       string `json:"body"`
			LinkType   string `json:"link_type"`
			LinkTarget string `json:"link_target"`
			Sort       int    `json:"sort"`
			Published  *bool  `json:"published"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		kind := strings.TrimSpace(body.Kind)
		if kind != "banner" && kind != "news" {
			api.Abort(c, http.StatusBadRequest, "kind 必须是 banner 或 news")
			return
		}
		title := strings.TrimSpace(body.Title)
		if title == "" {
			api.Abort(c, http.StatusBadRequest, "标题不能为空")
			return
		}
		published := true
		if body.Published != nil {
			published = *body.Published
		}
		now := api.NowMS()
		row := models.ContentItem{
			Kind:       kind,
			Title:      title,
			Summary:    strings.TrimSpace(body.Summary),
			Body:       body.Body,
			LinkType:   normalizeLinkType(body.LinkType),
			LinkTarget: strings.TrimSpace(body.LinkTarget),
			Sort:       body.Sort,
			Published:  published,
			CreatedAt:  now,
			UpdatedAt:  now,
		}
		if err := e.DB.Create(&row).Error; err != nil {
			api.Abort(c, http.StatusInternalServerError, "创建失败")
			return
		}
		c.JSON(http.StatusOK, api.ContentJSON(&row))
	}
}

func updateContent(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			Title      *string `json:"title"`
			Summary    *string `json:"summary"`
			Body       *string `json:"body"`
			LinkType   *string `json:"link_type"`
			LinkTarget *string `json:"link_target"`
			Sort       *int    `json:"sort"`
			Published  *bool   `json:"published"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		id, err := strconv.ParseUint(c.Param("id"), 10, 64)
		if err != nil {
			api.Abort(c, http.StatusNotFound, "内容不存在")
			return
		}
		var row models.ContentItem
		if e.DB.Where("id = ?", id).Take(&row).Error != nil {
			api.Abort(c, http.StatusNotFound, "内容不存在")
			return
		}
		updates := map[string]any{}
		if body.Title != nil {
			title := strings.TrimSpace(*body.Title)
			if title == "" {
				api.Abort(c, http.StatusBadRequest, "标题不能为空")
				return
			}
			updates["title"] = title
		}
		if body.Summary != nil {
			updates["summary"] = strings.TrimSpace(*body.Summary)
		}
		if body.Body != nil {
			updates["body"] = *body.Body
		}
		if body.LinkType != nil {
			updates["link_type"] = normalizeLinkType(*body.LinkType)
		}
		if body.LinkTarget != nil {
			updates["link_target"] = strings.TrimSpace(*body.LinkTarget)
		}
		if body.Sort != nil {
			updates["sort"] = *body.Sort
		}
		if body.Published != nil {
			updates["published"] = *body.Published
		}
		if len(updates) > 0 {
			updates["updated_at"] = api.NowMS()
			e.DB.Model(&models.ContentItem{}).Where("id = ?", row.ID).Updates(updates)
		}
		refetchContent(c, e, row.ID)
	}
}

func deleteContent(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		id, err := strconv.ParseUint(c.Param("id"), 10, 64)
		if err != nil {
			api.Abort(c, http.StatusNotFound, "内容不存在")
			return
		}
		var row models.ContentItem
		if e.DB.Where("id = ?", id).Take(&row).Error != nil {
			api.Abort(c, http.StatusNotFound, "内容不存在")
			return
		}
		if row.Cover != "" {
			_ = os.Remove(filepath.Join(e.Cfg.CoversDir(), row.Cover))
		}
		e.DB.Where("id = ?", row.ID).Delete(&models.ContentItem{})
		c.JSON(http.StatusOK, gin.H{"deleted": row.ID})
	}
}

func uploadContentCover(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		id, err := strconv.ParseUint(c.Param("id"), 10, 64)
		if err != nil {
			api.Abort(c, http.StatusNotFound, "内容不存在")
			return
		}
		var row models.ContentItem
		if e.DB.Where("id = ?", id).Take(&row).Error != nil {
			api.Abort(c, http.StatusNotFound, "内容不存在")
			return
		}
		file, err := c.FormFile("file")
		if err != nil {
			api.Abort(c, http.StatusBadRequest, "请选择封面图片")
			return
		}
		ext := strings.ToLower(filepath.Ext(file.Filename))
		if !api.IsImageExt(ext) {
			api.Abort(c, http.StatusBadRequest, "封面必须是 png/jpg/webp")
			return
		}
		name := fmt.Sprintf("content-%d-%s%s", row.ID, api.RandomHex(4), ext)
		if err := c.SaveUploadedFile(file, filepath.Join(e.Cfg.CoversDir(), name)); err != nil {
			api.Abort(c, http.StatusInternalServerError, "封面上传失败")
			return
		}
		if row.Cover != "" {
			_ = os.Remove(filepath.Join(e.Cfg.CoversDir(), row.Cover))
		}
		e.DB.Model(&models.ContentItem{}).Where("id = ?", row.ID).
			Updates(map[string]any{"cover": name, "updated_at": api.NowMS()})
		refetchContent(c, e, row.ID)
	}
}

func getHomeConfig(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{
			"miniapp_id": e.SettingValue(api.SettingHomeMiniApp),
		})
	}
}

func updateHomeConfig(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			MiniAppID string `json:"miniapp_id"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		id := strings.TrimSpace(body.MiniAppID)
		if id != "" {
			var m models.MiniApp
			if e.DB.Where("id = ?", id).Take(&m).Error != nil {
				api.Abort(c, http.StatusBadRequest, "指定的首页小程序不存在")
				return
			}
		}
		value := models.Setting{Key: api.SettingHomeMiniApp, Value: id}
		if err := e.DB.Save(&value).Error; err != nil {
			api.Abort(c, http.StatusInternalServerError, "保存失败")
			return
		}
		c.JSON(http.StatusOK, gin.H{"miniapp_id": id})
	}
}

func normalizeLinkType(value string) string {
	switch strings.TrimSpace(value) {
	case "miniapp", "news", "url":
		return strings.TrimSpace(value)
	default:
		return "none"
	}
}

func refetchContent(c *gin.Context, e *api.Env, id uint) {
	var fresh models.ContentItem
	if err := e.DB.Where("id = ?", id).Take(&fresh).Error; err != nil {
		api.Abort(c, http.StatusNotFound, "内容不存在")
		return
	}
	c.JSON(http.StatusOK, api.ContentJSON(&fresh))
}
