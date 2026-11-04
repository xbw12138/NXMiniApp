package api

import (
	"bytes"
	"io/fs"
	"net/http"
	"path"
	"strings"
	"time"

	"github.com/gin-gonic/gin"

	"nxminiapp/server/web"
)

// servePortal serves one embedded portal page (index.html / submit.html).
func servePortal(c *gin.Context, name string) {
	portal, err := web.Portal()
	if err != nil {
		c.String(http.StatusServiceUnavailable, "portal not built")
		return
	}
	data, err := fs.ReadFile(portal, name)
	if err != nil {
		Abort(c, http.StatusNotFound, "Not Found")
		return
	}
	http.ServeContent(c.Writer, c.Request, name, time.Time{}, bytes.NewReader(data))
}

// registerSPA serves the embedded admin SPA: any GET that is neither /api nor
// /media falls back to index.html so history-mode routing works.
func registerSPA(r *gin.Engine) {
	dist, err := web.Dist()
	if err != nil {
		dist = nil
	}
	r.NoRoute(func(c *gin.Context) {
		if c.Request.Method != http.MethodGet {
			Abort(c, http.StatusNotFound, "Not Found")
			return
		}
		p := c.Request.URL.Path
		if strings.HasPrefix(p, "/api/") || strings.HasPrefix(p, "/media/") {
			Abort(c, http.StatusNotFound, "Not Found")
			return
		}
		if dist == nil {
			c.String(http.StatusServiceUnavailable, "admin ui not built")
			return
		}
		name := strings.TrimPrefix(p, "/")
		name = strings.TrimPrefix(name, "admin/")
		name = path.Clean(name)
		if name == "." || name == "" {
			name = "index.html"
		}
		data, err := fs.ReadFile(dist, name)
		if err != nil {
			name = "index.html"
			data, err = fs.ReadFile(dist, "index.html")
		}
		if err != nil {
			c.String(http.StatusServiceUnavailable, "admin ui not built")
			return
		}
		http.ServeContent(c.Writer, c.Request, name, time.Time{}, bytes.NewReader(data))
	})
}
