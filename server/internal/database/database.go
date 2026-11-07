package database

import (
	"time"

	"gorm.io/driver/mysql"
	"gorm.io/gorm"

	"nxminiapp/server/internal/models"
)

// Connect opens the MySQL connection, retrying for up to 30 seconds while
// the database container is still starting.
func Connect(dsn string) (*gorm.DB, error) {
	var lastErr error
	for attempt := 0; attempt < 30; attempt++ {
		db, err := gorm.Open(mysql.Open(dsn), &gorm.Config{})
		if err == nil {
			sqlDB, derr := db.DB()
			if derr != nil {
				lastErr = derr
			} else if perr := sqlDB.Ping(); perr != nil {
				lastErr = perr
			} else {
				return db, nil
			}
		} else {
			lastErr = err
		}
		time.Sleep(time.Second)
	}
	return nil, lastErr
}

// Migrate creates or updates the schema for all store tables.
func Migrate(db *gorm.DB) error {
	return db.AutoMigrate(&models.StoreApp{}, &models.Version{}, &models.MiniApp{},
		&models.MiniAppOpen{}, &models.DocPage{}, &models.ContainerRelease{}, &models.ContentItem{}, &models.Setting{})
}
