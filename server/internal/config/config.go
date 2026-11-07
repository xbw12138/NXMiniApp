package config

import (
	"os"
	"path/filepath"
)

// Config holds the runtime configuration loaded from environment variables.
type Config struct {
	DataDir    string
	AdminToken string
	// 后台登录：密码 + TOTP 两步。为空时只保留 token 登录（兼容旧部署）。
	AdminUser         string
	AdminPasswordHash string
	AdminTOTPSecret   string
	DBDSN             string
	SeedDir           string
	DocsDir           string
}

func getenv(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}

// Load reads the configuration from the process environment.
func Load() Config {
	return Config{
		DataDir:           getenv("NXKIT_DATA_DIR", "/data"),
		AdminToken:        getenv("NXKIT_ADMIN_TOKEN", "nxkit-store-admin"),
		AdminUser:         getenv("NXKIT_ADMIN_USER", "admin"),
		AdminPasswordHash: getenv("NXKIT_ADMIN_PASSWORD_HASH", ""),
		AdminTOTPSecret:   getenv("NXKIT_ADMIN_TOTP_SECRET", ""),
		DBDSN: getenv("DB_DSN",
			"nxkit:nxkit@tcp(127.0.0.1:3306)/nxkit_store?parseTime=true&charset=utf8mb4&collation=utf8mb4_unicode_ci"),
		SeedDir: getenv("NXKIT_SEED_DIR", "/seed/miniapps"),
		DocsDir: getenv("NXKIT_DOCS_DIR", "/seed/docs"),
	}
}

func (c Config) FilesDir() string       { return filepath.Join(c.DataDir, "files") }
func (c Config) IconsDir() string       { return filepath.Join(c.DataDir, "icons") }
func (c Config) ScreenshotsDir() string { return filepath.Join(c.DataDir, "screenshots") }
func (c Config) MiniAppsDir() string    { return filepath.Join(c.DataDir, "miniapps") }
func (c Config) ReleasesDir() string    { return filepath.Join(c.DataDir, "releases") }
func (c Config) CoversDir() string      { return filepath.Join(c.DataDir, "covers") }

// EnsureDirs creates the data directory layout used by the server.
func (c Config) EnsureDirs() error {
	dirs := []string{c.DataDir, c.FilesDir(), c.IconsDir(), c.ScreenshotsDir(),
		c.MiniAppsDir(), c.ReleasesDir(), c.CoversDir()}
	for _, dir := range dirs {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}
	return nil
}
