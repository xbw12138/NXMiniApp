package api

import (
	"io/fs"
	"net/http"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

	"nxminiapp/server/internal/config"
	"nxminiapp/server/internal/docs"
	"nxminiapp/server/web"
)

func corsMiddleware() gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Header("Access-Control-Allow-Origin", "*")
		c.Header("Access-Control-Allow-Methods", "*")
		c.Header("Access-Control-Allow-Headers", "*")
		if c.Request.Method == http.MethodOptions {
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
	v1.GET("/content/:id", e.ContentDetail)
	v1.GET("/miniapps/:id", e.MiniAppDetail)
	v1.GET("/miniapps/:id/download", e.DownloadMiniApp)
	v1.GET("/miniapps/:id/icon", e.MiniAppIcon)
	v1.POST("/submissions", e.SubmitMiniApp)
	v1.GET("/container/releases", e.ListContainerReleases)
	v1.GET("/container/releases/:id/download", e.DownloadContainerRelease)

	r.Static("/media/icons", cfg.IconsDir())
	r.Static("/media/screenshots", cfg.ScreenshotsDir())
	r.Static("/media/covers", cfg.CoversDir())

	// 门户首页与投稿页（嵌入静态页，优先于 SPA 兜底路由）。
	r.GET("/", func(c *gin.Context) { servePortal(c, "index.html") })
	r.GET("/submit", func(c *gin.Context) { servePortal(c, "submit.html") })
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
