package config

import (
	"os"
	"path/filepath"
)

// Config holds the runtime configuration loaded from environment variables.
type Config struct {
	DataDir    string
	AdminToken string
	DBDSN      string
	SeedDir    string
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
		DataDir:    getenv("NXKIT_DATA_DIR", "/data"),
		AdminToken: getenv("NXKIT_ADMIN_TOKEN", "nxkit-store-admin"),
		DBDSN: getenv("DB_DSN",
			"nxkit:nxkit@tcp(127.0.0.1:3306)/nxkit_store?parseTime=true&charset=utf8mb4&collation=utf8mb4_unicode_ci"),
		SeedDir: getenv("NXKIT_SEED_DIR", "/seed/miniapps"),
	}
}

func (c Config) FilesDir() string       { return filepath.Join(c.DataDir, "files") }
func (c Config) IconsDir() string       { return filepath.Join(c.DataDir, "icons") }
func (c Config) ScreenshotsDir() string { return filepath.Join(c.DataDir, "screenshots") }
func (c Config) MiniAppsDir() string    { return filepath.Join(c.DataDir, "miniapps") }

// EnsureDirs creates the data directory layout used by the server.
func (c Config) EnsureDirs() error {
	dirs := []string{c.DataDir, c.FilesDir(), c.IconsDir(), c.ScreenshotsDir(), c.MiniAppsDir()}
	for _, dir := range dirs {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}
	return nil
}
