// Package api implements the public store and mini program endpoints and the
// shared helpers used by the admin API.
package api

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"net/url"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

	"nxminiapp/server/internal/config"
	"nxminiapp/server/internal/models"
)

// AppVersion is reported by /api/v1/health.
const AppVersion = "1.0.0"

// IDPattern validates store app and mini program identifiers.
var IDPattern = regexp.MustCompile(`^[a-z0-9][a-z0-9_-]{1,63}$`)

var nonSlugChars = regexp.MustCompile(`[^a-zA-Z0-9._-]`)
var versionDigits = regexp.MustCompile(`\d+`)

var imageExtensions = map[string]bool{
	".png":  true,
	".jpg":  true,
	".jpeg": true,
	".webp": true,
}

// Env carries the shared dependencies of all HTTP handlers.
type Env struct {
	DB  *gorm.DB
	Cfg config.Config
}

// Abort responds with a FastAPI-style {"detail": "..."} error.
func Abort(c *gin.Context, status int, detail string) {
	c.AbortWithStatusJSON(status, gin.H{"detail": detail})
}

// NowMS returns the current Unix timestamp in milliseconds.
func NowMS() int64 {
	return time.Now().UnixMilli()
}

// RandomHex returns n random bytes hex encoded (2n characters).
func RandomHex(n int) string {
	buf := make([]byte, n)
	if _, err := rand.Read(buf); err != nil {
		panic(err)
	}
	return hex.EncodeToString(buf)
}

// SlugFor sanitizes a value for use inside a download file name.
func SlugFor(value string) string {
	cleaned := strings.Trim(nonSlugChars.ReplaceAllString(value, "-"), "-")
	if cleaned == "" {
		return RandomHex(4)
	}
	return cleaned
}

// IsImageExt reports whether ext (lower case, with dot) is an allowed image.
func IsImageExt(ext string) bool {
	return imageExtensions[ext]
}

// FileExists reports whether path exists and is a regular file.
func FileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}

// DirExists reports whether path exists and is a directory.
func DirExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && info.IsDir()
}

// PositiveInt parses a positive integer query value with a fallback.
func PositiveInt(raw string, fallback int) int {
	if raw == "" {
		return fallback
	}
	n, err := strconv.Atoi(raw)
	if err != nil || n < 1 {
		return fallback
	}
	return n
}

// Sha256File streams a file through SHA-256 and returns the hex digest.
func Sha256File(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	digest := sha256.New()
	if _, err := io.Copy(digest, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(digest.Sum(nil)), nil
}

// versionKey mirrors the FastAPI version_key(): every numeric run of the
// version string as an integer tuple.
func versionKey(value string) []int {
	parts := versionDigits.FindAllString(value, -1)
	key := make([]int, 0, len(parts))
	for _, part := range parts {
		n, err := strconv.Atoi(part)
		if err != nil {
			n = 0
		}
		key = append(key, n)
	}
	return key
}

func compareVersionKey(a, b []int) int {
	for i := 0; i < len(a) && i < len(b); i++ {
		if a[i] != b[i] {
			if a[i] < b[i] {
				return -1
			}
			return 1
		}
	}
	switch {
	case len(a) < len(b):
		return -1
	case len(a) > len(b):
		return 1
	}
	return 0
}

func compareVersions(a, b *models.Version) int {
	if c := compareVersionKey(versionKey(a.Version), versionKey(b.Version)); c != 0 {
		return c
	}
	switch {
	case a.CreatedAt > b.CreatedAt:
		return 1
	case a.CreatedAt < b.CreatedAt:
		return -1
	}
	return 0
}

// LatestVersion picks the newest version by version_key, breaking ties with
// created_at, exactly like the FastAPI implementation.
func LatestVersion(db *gorm.DB, appID string) *models.Version {
	versions := make([]models.Version, 0)
	if err := db.Where("app_id = ?", appID).Find(&versions).Error; err != nil || len(versions) == 0 {
		return nil
	}
	best := versions[0]
	for i := 1; i < len(versions); i++ {
		if compareVersions(&versions[i], &best) > 0 {
			best = versions[i]
		}
	}
	return &best
}

// VersionJSON serializes a version row to the public contract.
func VersionJSON(appID string, v *models.Version) gin.H {
	downloadURL := "/api/v1/apps/" + url.PathEscape(appID) +
		"/download?version=" + url.PathEscape(v.Version)
	return gin.H{
		"id":           v.ID,
		"version":      v.Version,
		"changelog":    v.Changelog,
		"size":         v.Size,
		"sha256":       v.SHA256,
		"created_at":   v.CreatedAt,
		"download_url": downloadURL,
	}
}

// AppJSON serializes an app row to the public contract; admin adds
// version_count.
func AppJSON(db *gorm.DB, app *models.StoreApp, admin bool) gin.H {
	names := make([]string, 0)
	if app.Screenshots != "" {
		_ = json.Unmarshal([]byte(app.Screenshots), &names)
	}
	screenshots := make([]string, 0, len(names))
	for _, name := range names {
		screenshots = append(screenshots, "/media/screenshots/"+name)
	}
	iconURL := ""
	if app.Icon != "" {
		iconURL = "/media/icons/" + app.Icon
	}
	var latest any
	if v := LatestVersion(db, app.ID); v != nil {
		latest = VersionJSON(app.ID, v)
	}
	payload := gin.H{
		"id":             app.ID,
		"title":          app.Title,
		"summary":        app.Summary,
		"description":    app.Description,
		"category":       app.Category,
		"developer":      app.Developer,
		"icon_url":       iconURL,
		"screenshots":    screenshots,
		"featured":       app.Featured,
		"published":      app.Published,
		"downloads":      app.Downloads,
		"created_at":     app.CreatedAt,
		"updated_at":     app.UpdatedAt,
		"latest_version": latest,
	}
	if admin {
		var count int64
		db.Model(&models.Version{}).Where("app_id = ?", app.ID).Count(&count)
		payload["version_count"] = count
	}
	return payload
}
