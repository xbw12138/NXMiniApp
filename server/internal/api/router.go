package api

import (
	"io/fs"
	"net/http"
	"os"
	"strings"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

	"nxminiapp/server/internal/config"
	"nxminiapp/server/internal/docs"
	"nxminiapp/server/web"
)

// defaultCORSOrigins 是允许跨域访问公开 API 的来源。
// 门户/文档/后台与 API 同源，正常情况下不会触发 CORS；
// 这里只放行官网域名和本地开发端口，避免任意站点携带用户 Cookie 调用接口。
var defaultCORSOrigins = []string{
	"https://ecfun.cc",
	"http://ecfun.cc",
	"https://www.ecfun.cc",
	"http://localhost:5173",
	"http://127.0.0.1:5173",
	"http://localhost:4173",
}

func corsMiddleware() gin.HandlerFunc {
	allowed := map[string]bool{}
	for _, origin := range defaultCORSOrigins {
		allowed[origin] = true
	}
	// NXKIT_CORS_ORIGINS 用逗号追加额外来源（部署时可覆盖）。
	if raw := strings.TrimSpace(os.Getenv("NXKIT_CORS_ORIGINS")); raw != "" {
		for _, origin := range strings.Split(raw, ",") {
			if origin = strings.TrimSpace(origin); origin != "" {
				allowed[origin] = true
			}
		}
	}

	return func(c *gin.Context) {
		origin := c.GetHeader("Origin")
		if origin != "" && allowed[origin] {
			c.Header("Access-Control-Allow-Origin", origin)
			c.Header("Access-Control-Allow-Credentials", "true")
			c.Header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS")
			c.Header("Access-Control-Allow-Headers", "Authorization, Content-Type, X-Device-Id")
			c.Header("Access-Control-Max-Age", "600")
			c.Header("Vary", "Origin")
		}
		if c.Request.Method == http.MethodOptions {
			if origin != "" && !allowed[origin] {
				c.AbortWithStatus(http.StatusForbidden)
				return
			}
			c.AbortWithStatus(http.StatusNoContent)
			return
		}
		c.Next()
	}
}

// NewRouter builds the gin engine with CORS, the public API, media files and
// the embedded admin SPA. Admin routes are registered separately by the
// admin package via RegisterAdminGroup.
func NewRouter(cfg config.Config, db *gorm.DB) (*gin.Engine, *Env) {
	gin.SetMode(gin.ReleaseMode)
	r := gin.New()
	r.Use(gin.Logger(), gin.Recovery(), corsMiddleware())

	e := &Env{DB: db, Cfg: cfg}

	v1 := r.Group("/api/v1")
	v1.GET("/health", e.Health)
	v1.GET("/categories", e.Categories)
	v1.GET("/home", e.Home)
	v1.GET("/apps", e.ListApps)
	v1.GET("/apps/:id", e.AppDetail)
	v1.GET("/apps/:id/download", e.DownloadApp)
	v1.POST("/check-updates", e.CheckUpdates)
	v1.GET("/miniapps", e.ListMiniApps)
	v1.GET("/miniapp-home", e.MiniAppHome)
	// 有线电视小程序的直播源（服务端抓取 + 缓存，客户端后台静默刷新）。
	v1.GET("/tv/channels", e.TVChannels)
	v1.GET("/content/:id", e.ContentDetail)
	v1.POST("/content/:id/open", e.OpenContent)
	v1.GET("/miniapps/:id", e.MiniAppDetail)
	v1.GET("/miniapps/:id/download", e.DownloadMiniApp)
	v1.POST("/miniapps/:id/open", e.OpenMiniApp)
	v1.GET("/miniapps/:id/icon", e.MiniAppIcon)
	v1.POST("/submissions", e.SubmitMiniApp)
	v1.GET("/container/releases", e.ListContainerReleases)
	v1.GET("/container/releases/:id/download", e.DownloadContainerRelease)
	// 真机 Updater 上传容器日志（按 X-Device-Id 分目录存放）。
	v1.POST("/device/logs", e.UploadDeviceLog)

	// 上传者（开发者）自助维护接口：Bearer dev_key 或 HttpOnly Cookie。
	dev := v1.Group("/dev")
	dev.POST("/session", e.DevSession)
	dev.DELETE("/session", e.DevLogout)
	devAuth := dev.Group("", e.DevAuth())
	devAuth.GET("/miniapps", e.DevListMiniApps)
	devAuth.POST("/miniapps/:id/package", e.DevUploadPackage)
	devAuth.PATCH("/miniapps/:id/rollout", e.DevUpdateRollout)
	devAuth.POST("/miniapps/:id/rollout/promote", e.DevPromoteRollout)
	devAuth.POST("/miniapps/:id/rollout/discard", e.DevDiscardRollout)
	devAuth.PATCH("/miniapps/:id/visibility", e.DevSetVisibility)

	r.Static("/media/icons", cfg.IconsDir())
	r.Static("/media/screenshots", cfg.ScreenshotsDir())
	r.Static("/media/covers", cfg.CoversDir())

	// 门户首页与投稿页（嵌入静态页，优先于 SPA 兜底路由）。
	r.GET("/", func(c *gin.Context) { servePortal(c, "index.html") })
	r.GET("/submit", func(c *gin.Context) { servePortal(c, "submit.html") })
	r.GET("/my", func(c *gin.Context) { servePortal(c, "my.html") })
	r.GET("/download", func(c *gin.Context) { servePortal(c, "download.html") })
	// 开发 Skill 直接打包下载，避免国内访问 GitHub 打不开。
	r.GET("/skill/nxminiapp-dev.zip", func(c *gin.Context) {
		portal, err := web.Portal()
		if err != nil {
			c.String(http.StatusServiceUnavailable, "portal not built")
			return
		}
		data, err := fs.ReadFile(portal, "nxminiapp-dev.zip")
		if err != nil {
			Abort(c, http.StatusNotFound, "Not Found")
			return
		}
		c.Header("Content-Disposition", `attachment; filename="nxminiapp-dev.zip"`)
		c.Data(http.StatusOK, "application/zip", data)
	})

	// 文档站：页面内容在数据库，assets 静态文件在 DocsDir。
	docs.Register(r, db, cfg.DocsDir)

	registerSPA(r)
	return r, e
}

// RegisterAdminGroup returns the router group the admin package mounts on.
func RegisterAdminGroup(r *gin.Engine) *gin.RouterGroup {
	return r.Group("/api/v1/admin")
}
