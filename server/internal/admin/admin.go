// Package admin implements the /api/v1/admin/* management endpoints guarded
// by the NXKIT_ADMIN_TOKEN bearer token.
package admin

import (
	"crypto/subtle"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"

	"nxminiapp/server/internal/api"
	"nxminiapp/server/internal/docs"
	"nxminiapp/server/internal/miniappstore"
	"nxminiapp/server/internal/models"
)

// Register mounts all admin routes on /api/v1/admin.
func Register(r *gin.Engine, e *api.Env) {
	g := api.RegisterAdminGroup(r)
	g.POST("/login", login(e))
	g.POST("/login/password", passwordLogin(e))
	g.POST("/login/totp", totpLogin(e))
	g.POST("/logout", logout)

	auth := g.Group("", authMiddleware(e))
	auth.GET("/session", sessionStatus)
	auth.GET("/stats", stats(e))
	auth.GET("/notifications", notifications(e))
	auth.GET("/apps", listApps(e))
	auth.POST("/apps", createApp(e))
	auth.GET("/apps/:id", getApp(e))
	auth.PUT("/apps/:id", updateApp(e))
	auth.DELETE("/apps/:id", deleteApp(e))
	auth.PATCH("/apps/:id/flags", patchFlags(e))
	auth.POST("/apps/:id/icon", uploadIcon(e))
	auth.POST("/apps/:id/screenshots", uploadScreenshots(e))
	auth.POST("/apps/:id/screenshots/delete", deleteScreenshot(e))
	auth.POST("/apps/:id/versions", uploadVersion(e))
	auth.DELETE("/apps/:id/versions/:version_id", deleteVersion(e))
	auth.GET("/miniapps", listMiniApps(e))
	auth.POST("/miniapps", createMiniApp(e))
	auth.PUT("/miniapps/:id", updateMiniApp(e))
	auth.DELETE("/miniapps/:id", deleteMiniApp(e))
	auth.PATCH("/miniapps/:id/flags", patchMiniAppFlags(e))
	auth.PATCH("/miniapps/:id/rollout", updateMiniAppRollout(e))
	auth.POST("/miniapps/:id/rollout/package", uploadGrayMiniAppPackage(e))
	auth.POST("/miniapps/:id/rollout/promote", promoteMiniAppRollout(e))
	auth.POST("/miniapps/:id/rollout/discard", discardMiniAppRollout(e))
	auth.POST("/miniapps/:id/icon", uploadMiniAppIcon(e))
	auth.POST("/miniapps/:id/package", uploadMiniAppPackage(e))
	auth.GET("/releases", listReleases(e))
	auth.POST("/releases", uploadRelease(e))
	auth.PATCH("/releases/:id/flags", patchReleaseFlags(e))
	auth.DELETE("/releases/:id", deleteRelease(e))
	// 真机 Updater 上传的容器日志。
	auth.GET("/device-logs", listDeviceLogs(e))
	auth.GET("/device-logs/content", getDeviceLog(e))
	auth.DELETE("/device-logs", deleteDeviceLog(e))
	auth.GET("/content", listContent(e))
	auth.POST("/content", createContent(e))
	auth.PUT("/content/:id", updateContent(e))
	auth.DELETE("/content/:id", deleteContent(e))
	auth.POST("/content/:id/cover", uploadContentCover(e))
	auth.GET("/home-config", getHomeConfig(e))
	auth.PUT("/home-config", updateHomeConfig(e))

	docs.RegisterAdmin(auth, e.DB, e.Cfg.DocsDir)
}

func authMiddleware(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		// 后台页面走 Cookie 会话；脚本与 CI 继续用 Bearer token。
		if cookieValid(c) {
			c.Next()
			return
		}
		header := c.GetHeader("Authorization")
		token := ""
		if len(header) > 7 && strings.EqualFold(header[:7], "bearer ") {
			token = header[7:]
		}
		if token == "" || subtle.ConstantTimeCompare([]byte(token), []byte(e.Cfg.AdminToken)) != 1 {
			api.Abort(c, http.StatusUnauthorized, "未登录或登录已过期")
			return
		}
		c.Next()
	}
}

func login(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			Token string `json:"token"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		if subtle.ConstantTimeCompare([]byte(body.Token), []byte(e.Cfg.AdminToken)) != 1 {
			api.Abort(c, http.StatusUnauthorized, "管理令牌无效")
			return
		}
		c.JSON(http.StatusOK, gin.H{"token": e.Cfg.AdminToken})
	}
}

func stats(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var miniapps, published, releases int64
		var downloads, opens, uv int64
		e.DB.Model(&models.MiniApp{}).Count(&miniapps)
		e.DB.Model(&models.MiniApp{}).Where("published = ?", true).Count(&published)
		e.DB.Model(&models.ContainerRelease{}).Count(&releases)
		_ = e.DB.Model(&models.MiniApp{}).Select("COALESCE(SUM(downloads), 0)").Row().Scan(&downloads)
		_ = e.DB.Model(&models.MiniApp{}).Select("COALESCE(SUM(opens), 0)").Row().Scan(&opens)
		_ = e.DB.Model(&models.MiniApp{}).Select("COALESCE(SUM(uv), 0)").Row().Scan(&uv)

		storage := miniappstore.DirSize(e.Cfg.MiniAppsDir()) + miniappstore.DirSize(e.Cfg.ReleasesDir())

		recent := make([]models.MiniApp, 0)
		e.DB.Order("updated_at DESC").Limit(6).Find(&recent)
		items := make([]gin.H, 0, len(recent))
		for i := range recent {
			items = append(items, api.MiniAppJSON(e.Cfg, &recent[i]))
		}
		c.JSON(http.StatusOK, gin.H{
			"miniapps":      miniapps,
			"published":     published,
			"releases":      releases,
			"downloads":     downloads,
			"opens":         opens,
			"uv":            uv,
			"storage_bytes": storage,
			"recent":        items,
		})
	}
}

// notifications 提供给后台顶栏轮询的轻量待办计数。
func notifications(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var pending int64
		if err := e.DB.Model(&models.MiniApp{}).Where("published = ?", false).Count(&pending).Error; err != nil {
			api.Abort(c, http.StatusInternalServerError, "读取待审数量失败")
			return
		}
		c.JSON(http.StatusOK, gin.H{"pending_miniapps": pending})
	}
}

func listApps(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		q := strings.TrimSpace(c.Query("q"))
		page := api.PositiveInt(c.Query("page"), 1)
		pageSize := api.PositiveInt(c.Query("page_size"), 50)
		if pageSize > 100 {
			pageSize = 100
		}

		query := e.DB.Model(&models.StoreApp{})
		if q != "" {
			needle := "%" + q + "%"
			query = query.Where("id LIKE ? OR title LIKE ? OR developer LIKE ?", needle, needle, needle)
		}
		var total int64
		query.Count(&total)

		rows := make([]models.StoreApp, 0)
		query.Order("updated_at DESC").Limit(pageSize).Offset((page - 1) * pageSize).Find(&rows)
		items := make([]gin.H, 0, len(rows))
		for i := range rows {
			items = append(items, api.AppJSON(e.DB, &rows[i], true))
		}
		c.JSON(http.StatusOK, gin.H{
			"items":     items,
			"total":     total,
			"page":      page,
			"page_size": pageSize,
		})
	}
}

func createApp(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			ID          string `json:"id"`
			Title       string `json:"title"`
			Summary     string `json:"summary"`
			Description string `json:"description"`
			Category    string `json:"category"`
			Developer   string `json:"developer"`
			Source      string `json:"source"`
			Featured    bool   `json:"featured"`
			Published   *bool  `json:"published"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		id := strings.ToLower(strings.TrimSpace(body.ID))
		if !api.IDPattern.MatchString(id) {
			api.Abort(c, http.StatusBadRequest, "应用 ID 仅支持小写字母、数字、- 和 _")
			return
		}
		title := strings.TrimSpace(body.Title)
		if title == "" {
			api.Abort(c, http.StatusBadRequest, "标题不能为空")
			return
		}
		var exists models.StoreApp
		if err := e.DB.Where("id = ?", id).Take(&exists).Error; err == nil {
			api.Abort(c, http.StatusConflict, "应用 ID 已存在")
			return
		}
		published := true
		if body.Published != nil {
			published = *body.Published
		}
		category := strings.TrimSpace(body.Category)
		if category == "" {
			category = "其他"
		}
		source := strings.TrimSpace(body.Source)
		if source != api.SourceContainer {
			source = api.SourceUpload
		}
		now := api.NowMS()
		app := models.StoreApp{
			ID:          id,
			Title:       title,
			Summary:     strings.TrimSpace(body.Summary),
			Description: body.Description,
			Category:    category,
			Developer:   strings.TrimSpace(body.Developer),
			Source:      source,
			Screenshots: "[]",
			Featured:    body.Featured,
			Published:   published,
			CreatedAt:   now,
			UpdatedAt:   now,
		}
		if err := e.DB.Create(&app).Error; err != nil {
			api.Abort(c, http.StatusInternalServerError, "创建失败")
			return
		}
		c.JSON(http.StatusOK, api.AppJSON(e.DB, &app, true))
	}
}

func getApp(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		app := e.GetApp(c, c.Param("id"), false)
		if app == nil {
			return
		}
		payload := api.AppJSON(e.DB, app, true)
		versions := make([]models.Version, 0)
		e.DB.Where("app_id = ?", app.ID).Order("created_at DESC").Find(&versions)
		items := make([]gin.H, 0, len(versions))
		for i := range versions {
			items = append(items, api.VersionJSON(app.ID, &versions[i]))
		}
		payload["versions"] = items
		c.JSON(http.StatusOK, payload)
	}
}

func updateApp(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			Title       *string `json:"title"`
			Summary     *string `json:"summary"`
			Description *string `json:"description"`
			Category    *string `json:"category"`
			Developer   *string `json:"developer"`
			Source      *string `json:"source"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		app := e.GetApp(c, c.Param("id"), false)
		if app == nil {
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
		if body.Description != nil {
			updates["description"] = *body.Description
		}
		if body.Category != nil {
			updates["category"] = strings.TrimSpace(*body.Category)
		}
		if body.Developer != nil {
			updates["developer"] = strings.TrimSpace(*body.Developer)
		}
		if body.Source != nil {
			source := strings.TrimSpace(*body.Source)
			if source != api.SourceContainer {
				source = api.SourceUpload
			}
			updates["source"] = source
		}
		if len(updates) > 0 {
			updates["updated_at"] = api.NowMS()
			e.DB.Model(&models.StoreApp{}).Where("id = ?", app.ID).Updates(updates)
		}
		refetchApp(c, e, app.ID)
	}
}

func deleteApp(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		app := e.GetApp(c, c.Param("id"), false)
		if app == nil {
			return
		}
		versions := make([]models.Version, 0)
		e.DB.Where("app_id = ?", app.ID).Find(&versions)
		for _, v := range versions {
			_ = os.Remove(filepath.Join(e.Cfg.FilesDir(), v.Filename))
		}
		_ = os.RemoveAll(filepath.Join(e.Cfg.FilesDir(), app.ID))
		if app.Icon != "" {
			_ = os.Remove(filepath.Join(e.Cfg.IconsDir(), app.Icon))
		}
		names := make([]string, 0)
		_ = json.Unmarshal([]byte(app.Screenshots), &names)
		for _, name := range names {
			_ = os.Remove(filepath.Join(e.Cfg.ScreenshotsDir(), name))
		}
		e.DB.Where("app_id = ?", app.ID).Delete(&models.Version{})
		e.DB.Where("id = ?", app.ID).Delete(&models.StoreApp{})
		c.JSON(http.StatusOK, gin.H{"deleted": app.ID})
	}
}

func patchFlags(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			Published *bool `json:"published"`
			Featured  *bool `json:"featured"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		app := e.GetApp(c, c.Param("id"), false)
		if app == nil {
			return
		}
		updates := map[string]any{"updated_at": api.NowMS()}
		if body.Published != nil {
			updates["published"] = *body.Published
		}
		if body.Featured != nil {
			updates["featured"] = *body.Featured
		}
		e.DB.Model(&models.StoreApp{}).Where("id = ?", app.ID).Updates(updates)
		refetchApp(c, e, app.ID)
	}
}

func uploadIcon(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		app := e.GetApp(c, c.Param("id"), false)
		if app == nil {
			return
		}
		file, err := c.FormFile("file")
		if err != nil {
			api.Abort(c, http.StatusBadRequest, "请选择图标文件")
			return
		}
		ext := strings.ToLower(filepath.Ext(file.Filename))
		if !api.IsImageExt(ext) {
			api.Abort(c, http.StatusBadRequest, "图标必须是 png/jpg/webp")
			return
		}
		name := fmt.Sprintf("%s-%s%s", app.ID, api.RandomHex(4), ext)
		if err := c.SaveUploadedFile(file, filepath.Join(e.Cfg.IconsDir(), name)); err != nil {
			api.Abort(c, http.StatusInternalServerError, "图标保存失败")
			return
		}
		if app.Icon != "" {
			_ = os.Remove(filepath.Join(e.Cfg.IconsDir(), app.Icon))
		}
		e.DB.Model(&models.StoreApp{}).Where("id = ?", app.ID).
			Updates(map[string]any{"icon": name, "updated_at": api.NowMS()})
		refetchApp(c, e, app.ID)
	}
}

func uploadScreenshots(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		app := e.GetApp(c, c.Param("id"), false)
		if app == nil {
			return
		}
		form, err := c.MultipartForm()
		if err != nil {
			api.Abort(c, http.StatusBadRequest, "请选择截图文件")
			return
		}
		files := form.File["files"]
		if len(files) == 0 {
			api.Abort(c, http.StatusBadRequest, "请选择截图文件")
			return
		}
		names := make([]string, 0)
		_ = json.Unmarshal([]byte(app.Screenshots), &names)
		if len(names)+len(files) > 10 {
			api.Abort(c, http.StatusBadRequest, "每个应用最多 10 张截图")
			return
		}
		for _, file := range files {
			ext := strings.ToLower(filepath.Ext(file.Filename))
			if !api.IsImageExt(ext) {
				api.Abort(c, http.StatusBadRequest, "截图必须是 png/jpg/webp")
				return
			}
			name := fmt.Sprintf("%s-%s%s", app.ID, api.RandomHex(5), ext)
			if err := c.SaveUploadedFile(file, filepath.Join(e.Cfg.ScreenshotsDir(), name)); err != nil {
				api.Abort(c, http.StatusInternalServerError, "截图保存失败")
				return
			}
			names = append(names, name)
		}
		raw, _ := json.Marshal(names)
		e.DB.Model(&models.StoreApp{}).Where("id = ?", app.ID).
			Updates(map[string]any{"screenshots": string(raw), "updated_at": api.NowMS()})
		refetchApp(c, e, app.ID)
	}
}

func deleteScreenshot(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			Filename string `json:"filename"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		app := e.GetApp(c, c.Param("id"), false)
		if app == nil {
			return
		}
		name := filepath.Base(body.Filename)
		names := make([]string, 0)
		_ = json.Unmarshal([]byte(app.Screenshots), &names)
		kept := make([]string, 0, len(names))
		for _, item := range names {
			if item != name {
				kept = append(kept, item)
			}
		}
		raw, _ := json.Marshal(kept)
		e.DB.Model(&models.StoreApp{}).Where("id = ?", app.ID).
			Updates(map[string]any{"screenshots": string(raw), "updated_at": api.NowMS()})
		_ = os.Remove(filepath.Join(e.Cfg.ScreenshotsDir(), name))
		refetchApp(c, e, app.ID)
	}
}

func uploadVersion(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		app := e.GetApp(c, c.Param("id"), false)
		if app == nil {
			return
		}
		version := strings.TrimSpace(c.PostForm("version"))
		if version == "" {
			api.Abort(c, http.StatusBadRequest, "版本号不能为空")
			return
		}
		changelog := c.PostForm("changelog")
		file, err := c.FormFile("file")
		if err != nil {
			api.Abort(c, http.StatusBadRequest, "请选择安装包文件")
			return
		}
		if strings.ToLower(filepath.Ext(file.Filename)) != ".nro" {
			api.Abort(c, http.StatusBadRequest, "安装包必须是 .nro 文件")
			return
		}
		var count int64
		e.DB.Model(&models.Version{}).Where("app_id = ? AND version = ?", app.ID, version).Count(&count)
		if count > 0 {
			api.Abort(c, http.StatusConflict, "该版本号已存在")
			return
		}

		name := fmt.Sprintf("%s-%s-%s.nro", api.SlugFor(app.ID), api.SlugFor(version), api.RandomHex(4))
		dst := filepath.Join(e.Cfg.FilesDir(), name)
		if err := c.SaveUploadedFile(file, dst); err != nil {
			api.Abort(c, http.StatusInternalServerError, "安装包保存失败")
			return
		}
		info, err := os.Stat(dst)
		if err != nil {
			api.Abort(c, http.StatusInternalServerError, "安装包保存失败")
			return
		}
		digest, err := api.Sha256File(dst)
		if err != nil {
			api.Abort(c, http.StatusInternalServerError, "安装包校验失败")
			return
		}

		row := models.Version{
			AppID:     app.ID,
			Version:   version,
			Changelog: changelog,
			Filename:  name,
			Size:      info.Size(),
			SHA256:    digest,
			CreatedAt: api.NowMS(),
		}
		if err := e.DB.Create(&row).Error; err != nil {
			_ = os.Remove(dst)
			api.Abort(c, http.StatusInternalServerError, "版本创建失败")
			return
		}
		e.DB.Model(&models.StoreApp{}).Where("id = ?", app.ID).Update("updated_at", api.NowMS())

		latest := api.LatestVersion(e.DB, app.ID)
		if latest == nil {
			c.JSON(http.StatusOK, gin.H{})
			return
		}
		c.JSON(http.StatusOK, api.VersionJSON(app.ID, latest))
	}
}

func deleteVersion(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		app := e.GetApp(c, c.Param("id"), false)
		if app == nil {
			return
		}
		versionID, err := strconv.ParseUint(c.Param("version_id"), 10, 64)
		if err != nil {
			api.Abort(c, http.StatusNotFound, "版本不存在")
			return
		}
		var v models.Version
		if err := e.DB.Where("id = ? AND app_id = ?", versionID, app.ID).Take(&v).Error; err != nil {
			api.Abort(c, http.StatusNotFound, "版本不存在")
			return
		}
		_ = os.Remove(filepath.Join(e.Cfg.FilesDir(), v.Filename))
		e.DB.Where("id = ?", v.ID).Delete(&models.Version{})
		e.DB.Model(&models.StoreApp{}).Where("id = ?", app.ID).Update("updated_at", api.NowMS())
		c.JSON(http.StatusOK, gin.H{"deleted": versionID})
	}
}

// refetchApp reloads the app row and writes the admin JSON response.
func refetchApp(c *gin.Context, e *api.Env, id string) {
	var fresh models.StoreApp
	if err := e.DB.Where("id = ?", id).Take(&fresh).Error; err != nil {
		api.Abort(c, http.StatusNotFound, "应用不存在")
		return
	}
	c.JSON(http.StatusOK, api.AppJSON(e.DB, &fresh, true))
}

func listMiniApps(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		rows := make([]models.MiniApp, 0)
		e.DB.Order("updated_at DESC").Find(&rows)
		items := make([]gin.H, 0, len(rows))
		for i := range rows {
			item := api.MiniAppJSON(e.Cfg, &rows[i])
			for key, value := range api.RolloutJSON(&rows[i]) {
				item[key] = value
			}
			items = append(items, item)
		}
		c.JSON(http.StatusOK, gin.H{"items": items})
	}
}

func createMiniApp(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			ID          string `json:"id"`
			Title       string `json:"title"`
			Summary     string `json:"summary"`
			Description string `json:"description"`
			Category    string `json:"category"`
			Developer   string `json:"developer"`
			Version     string `json:"version"`
			Published   *bool  `json:"published"`
			Featured    *bool  `json:"featured"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		id := strings.ToLower(strings.TrimSpace(body.ID))
		if !api.IDPattern.MatchString(id) {
			api.Abort(c, http.StatusBadRequest, "小程序 ID 仅支持小写字母、数字、- 和 _")
			return
		}
		title := strings.TrimSpace(body.Title)
		if title == "" {
			api.Abort(c, http.StatusBadRequest, "标题不能为空")
			return
		}
		var exists models.MiniApp
		if err := e.DB.Where("id = ?", id).Take(&exists).Error; err == nil {
			api.Abort(c, http.StatusConflict, "小程序 ID 已存在")
			return
		}
		published := true
		if body.Published != nil {
			published = *body.Published
		}
		category := strings.TrimSpace(body.Category)
		if category == "" {
			category = "其他"
		}
		version := strings.TrimSpace(body.Version)
		if version == "" {
			version = "1.0.0"
		}
		now := api.NowMS()
		featured := false
		if body.Featured != nil {
			featured = *body.Featured
		}
		row := models.MiniApp{
			ID:             id,
			Title:          title,
			VisiblePercent: 100,
			Summary:        strings.TrimSpace(body.Summary),
			Description:    body.Description,
			Category:       category,
			Developer:      strings.TrimSpace(body.Developer),
			Version:        version,
			Published:      published,
			Featured:       featured,
			CreatedAt:      now,
			UpdatedAt:      now,
		}
		if err := e.DB.Create(&row).Error; err != nil {
			api.Abort(c, http.StatusInternalServerError, "创建失败")
			return
		}
		_ = os.MkdirAll(miniappstore.PackageDir(e.Cfg.DataDir, id), 0o755)
		c.JSON(http.StatusOK, api.MiniAppJSON(e.Cfg, &row))
	}
}

func updateMiniApp(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			Title       *string `json:"title"`
			Summary     *string `json:"summary"`
			Description *string `json:"description"`
			Category    *string `json:"category"`
			Developer   *string `json:"developer"`
			Version     *string `json:"version"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		m := e.GetMiniApp(c, c.Param("id"))
		if m == nil {
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
		if body.Description != nil {
			updates["description"] = *body.Description
		}
		if body.Category != nil {
			updates["category"] = strings.TrimSpace(*body.Category)
		}
		if body.Developer != nil {
			updates["developer"] = strings.TrimSpace(*body.Developer)
		}
		if body.Version != nil {
			updates["version"] = strings.TrimSpace(*body.Version)
		}
		if len(updates) > 0 {
			updates["updated_at"] = api.NowMS()
			e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(updates)
		}
		refetchMiniApp(c, e, m.ID)
	}
}

func deleteMiniApp(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		m := e.GetMiniApp(c, c.Param("id"))
		if m == nil {
			return
		}
		e.DB.Where("id = ?", m.ID).Delete(&models.MiniApp{})
		_ = os.RemoveAll(miniappstore.MiniAppDir(e.Cfg.DataDir, m.ID))
		c.JSON(http.StatusOK, gin.H{"deleted": m.ID})
	}
}

func patchMiniAppFlags(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			Published *bool `json:"published"`
			Featured  *bool `json:"featured"`
			Sort      *int  `json:"sort"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		m := e.GetMiniApp(c, c.Param("id"))
		if m == nil {
			return
		}
		updates := map[string]any{"updated_at": api.NowMS()}
		if body.Published != nil {
			updates["published"] = *body.Published
		}
		if body.Featured != nil {
			updates["featured"] = *body.Featured
		}
		if body.Sort != nil {
			updates["sort"] = *body.Sort
		}
		e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(updates)
		refetchMiniApp(c, e, m.ID)
	}
}

func uploadMiniAppIcon(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		m := e.GetMiniApp(c, c.Param("id"))
		if m == nil {
			return
		}
		file, err := c.FormFile("file")
		if err != nil {
			api.Abort(c, http.StatusBadRequest, "请选择图标文件")
			return
		}
		ext := strings.ToLower(filepath.Ext(file.Filename))
		if !api.IsImageExt(ext) {
			api.Abort(c, http.StatusBadRequest, "图标必须是 png/jpg/webp")
			return
		}
		dir := miniappstore.MiniAppDir(e.Cfg.DataDir, m.ID)
		if err := os.MkdirAll(dir, 0o755); err != nil {
			api.Abort(c, http.StatusInternalServerError, "图标保存失败")
			return
		}
		name := "icon" + ext
		if err := c.SaveUploadedFile(file, filepath.Join(dir, name)); err != nil {
			api.Abort(c, http.StatusInternalServerError, "图标保存失败")
			return
		}
		if m.Icon != "" && m.Icon != name {
			_ = os.Remove(filepath.Join(dir, m.Icon))
		}
		e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).
			Updates(map[string]any{"icon": name, "updated_at": api.NowMS()})
		refetchMiniApp(c, e, m.ID)
	}
}

func uploadMiniAppPackage(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		m := e.GetMiniApp(c, c.Param("id"))
		if m == nil {
			return
		}
		file, err := c.FormFile("file")
		if err != nil {
			api.Abort(c, http.StatusBadRequest, "请选择小程序包")
			return
		}
		ext := strings.ToLower(filepath.Ext(file.Filename))
		if ext != ".nxpkg" && ext != ".zip" {
			api.Abort(c, http.StatusBadRequest, "安装包必须是 .nxpkg 或 .zip 文件")
			return
		}
		version := strings.TrimSpace(c.PostForm("version"))

		tmp, err := os.CreateTemp("", "nxpkg-upload-*")
		if err != nil {
			api.Abort(c, http.StatusInternalServerError, "安装包保存失败")
			return
		}
		tmpPath := tmp.Name()
		_ = tmp.Close()
		defer os.Remove(tmpPath)

		if err := c.SaveUploadedFile(file, tmpPath); err != nil {
			api.Abort(c, http.StatusInternalServerError, "安装包保存失败")
			return
		}
		if err := miniappstore.ExtractPackage(tmpPath, miniappstore.PackageDir(e.Cfg.DataDir, m.ID)); err != nil {
			api.Abort(c, http.StatusBadRequest, err.Error())
			return
		}

		updates := map[string]any{"updated_at": api.NowMS()}
		if version != "" {
			updates["version"] = version
		}
		e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(updates)
		refetchMiniApp(c, e, m.ID)
	}
}

// updateMiniAppRollout 调整可见比例与版本灰度参数（不涉及安装包）。
func updateMiniAppRollout(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		m := e.GetMiniApp(c, c.Param("id"))
		if m == nil {
			return
		}
		var body struct {
			VisiblePercent   *int    `json:"visible_percent"`
			VisibleWhitelist *string `json:"visible_whitelist"`
			GrayPercent      *int    `json:"gray_percent"`
			GrayWhitelist    *string `json:"gray_whitelist"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "参数错误")
			return
		}
		updates := map[string]any{"updated_at": api.NowMS()}
		if body.VisiblePercent != nil {
			updates["visible_percent"] = api.ClampPercent(fmt.Sprint(*body.VisiblePercent))
		}
		if body.VisibleWhitelist != nil {
			updates["visible_whitelist"] = api.NormalizeDevices(*body.VisibleWhitelist)
		}
		if body.GrayPercent != nil {
			updates["gray_percent"] = api.ClampPercent(fmt.Sprint(*body.GrayPercent))
		}
		if body.GrayWhitelist != nil {
			updates["gray_whitelist"] = api.NormalizeDevices(*body.GrayWhitelist)
		}
		e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(updates)
		refetchMiniApp(c, e, m.ID)
	}
}

// uploadGrayMiniAppPackage 上传灰度版本包，只下发给命中的设备。
func uploadGrayMiniAppPackage(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		m := e.GetMiniApp(c, c.Param("id"))
		if m == nil {
			return
		}
		file, err := c.FormFile("file")
		if err != nil {
			api.Abort(c, http.StatusBadRequest, "请选择小程序包")
			return
		}
		ext := strings.ToLower(filepath.Ext(file.Filename))
		if ext != ".nxpkg" && ext != ".zip" {
			api.Abort(c, http.StatusBadRequest, "安装包必须是 .nxpkg 或 .zip 文件")
			return
		}
		version := strings.TrimSpace(c.PostForm("version"))
		if version == "" {
			api.Abort(c, http.StatusBadRequest, "请填写灰度版本号")
			return
		}

		tmp, err := os.CreateTemp("", "nxpkg-gray-*")
		if err != nil {
			api.Abort(c, http.StatusInternalServerError, "安装包保存失败")
			return
		}
		tmpPath := tmp.Name()
		_ = tmp.Close()
		defer os.Remove(tmpPath)
		if err := c.SaveUploadedFile(file, tmpPath); err != nil {
			api.Abort(c, http.StatusInternalServerError, "安装包保存失败")
			return
		}

		grayDir := miniappstore.GrayPackageDir(e.Cfg.DataDir, m.ID)
		_ = os.RemoveAll(grayDir)
		if err := miniappstore.ExtractPackage(tmpPath, grayDir); err != nil {
			api.Abort(c, http.StatusBadRequest, err.Error())
			return
		}

		e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(map[string]any{
			"gray_version":   version,
			"gray_percent":   api.ClampPercent(c.PostForm("percent")),
			"gray_whitelist": api.NormalizeDevices(c.PostForm("whitelist")),
			"updated_at":     api.NowMS(),
		})
		refetchMiniApp(c, e, m.ID)
	}
}

// promoteMiniAppRollout 灰度转全量：灰度包替换稳定包，清空灰度状态。
func promoteMiniAppRollout(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		m := e.GetMiniApp(c, c.Param("id"))
		if m == nil {
			return
		}
		if strings.TrimSpace(m.GrayVersion) == "" {
			api.Abort(c, http.StatusBadRequest, "当前没有灰度版本")
			return
		}
		grayDir := miniappstore.GrayPackageDir(e.Cfg.DataDir, m.ID)
		if info, err := os.Stat(grayDir); err != nil || !info.IsDir() {
			api.Abort(c, http.StatusBadRequest, "灰度包不存在")
			return
		}
		stableDir := miniappstore.PackageDir(e.Cfg.DataDir, m.ID)
		_ = os.RemoveAll(stableDir)
		if err := os.Rename(grayDir, stableDir); err != nil {
			api.Abort(c, http.StatusInternalServerError, "全量发布失败")
			return
		}
		e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(map[string]any{
			"version":        m.GrayVersion,
			"gray_version":   "",
			"gray_percent":   0,
			"gray_whitelist": "",
			"updated_at":     api.NowMS(),
		})
		refetchMiniApp(c, e, m.ID)
	}
}

// discardMiniAppRollout 撤销灰度，设备回到稳定版本。
func discardMiniAppRollout(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		m := e.GetMiniApp(c, c.Param("id"))
		if m == nil {
			return
		}
		_ = os.RemoveAll(miniappstore.GrayPackageDir(e.Cfg.DataDir, m.ID))
		e.DB.Model(&models.MiniApp{}).Where("id = ?", m.ID).Updates(map[string]any{
			"gray_version":   "",
			"gray_percent":   0,
			"gray_whitelist": "",
			"updated_at":     api.NowMS(),
		})
		refetchMiniApp(c, e, m.ID)
	}
}

// refetchMiniApp reloads the mini program row and writes its JSON response.
func refetchMiniApp(c *gin.Context, e *api.Env, id string) {
	var fresh models.MiniApp
	if err := e.DB.Where("id = ?", id).Take(&fresh).Error; err != nil {
		api.Abort(c, http.StatusNotFound, "mini app not found")
		return
	}
	item := api.MiniAppJSON(e.Cfg, &fresh)
	for key, value := range api.RolloutJSON(&fresh) {
		item[key] = value
	}
	c.JSON(http.StatusOK, item)
}
