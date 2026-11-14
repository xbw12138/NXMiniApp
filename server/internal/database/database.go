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

// migrateReviewStatus 修正早期迁移的误判：published=false 并不等于待审核，
// 内置示例与后台手动下线的小程序也会是 false。这里把「没有待审核包」的记录
// 恢复为已通过审核——下线状态不变，只是不再出现在待审核队列。
// 新投稿/新版本一定带有 pending_kind 与 pending_version，不会被误伤。
func migrateReviewStatus(db *gorm.DB) error {
	const flag = "migration.review_status_v2"
	var row models.Setting
	if err := db.Where("`key` = ?", flag).Take(&row).Error; err == nil {
		return nil
	}
	if err := db.Model(&models.MiniApp{}).
		Where("review_status = ? AND pending_version = ? AND pending_kind = ?",
			models.ReviewPending, "", "").
		Update("review_status", models.ReviewApproved).Error; err != nil {
		return err
	}
	return db.Create(&models.Setting{Key: flag, Value: "1"}).Error
}
