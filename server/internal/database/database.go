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
	if err := db.AutoMigrate(&models.StoreApp{}, &models.Version{}, &models.MiniApp{},
		&models.MiniAppOpen{}, &models.DocPage{}, &models.ContainerRelease{}, &models.ContentItem{},
		&models.ContentOpen{}, &models.Setting{}); err != nil {
		return err
	}
	return migrateReviewStatus(db)
}

// migrateReviewStatus 把旧的「published=false 即待审核」数据迁移成新状态机。
// 用 settings 里的标记保证只执行一次，避免把后台手动下线的小程序误判为待审核。
func migrateReviewStatus(db *gorm.DB) error {
	const flag = "migration.review_status_v1"
	var row models.Setting
	if err := db.Where("`key` = ?", flag).Take(&row).Error; err == nil {
		return nil
	}
	if err := db.Model(&models.MiniApp{}).Where("published = ?", false).
		Update("review_status", models.ReviewPending).Error; err != nil {
		return err
	}
	return db.Create(&models.Setting{Key: flag, Value: "1"}).Error
}
