// Package web embeds the built admin SPA (web/admin/dist, produced by the
// Vite build stage of the Dockerfile).
package web

import (
	"embed"
	"io/fs"
)

//go:embed admin/dist
var dist embed.FS

// Dist returns the admin SPA build output rooted at the dist directory.
func Dist() (fs.FS, error) {
	return fs.Sub(dist, "admin/dist")
}
