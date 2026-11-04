package models

// StoreApp is a Switch NRO application listed in the store.
// All timestamps are Unix milliseconds.
type StoreApp struct {
	ID          string `gorm:"primaryKey;size:64"`
	Title       string `gorm:"size:255;not null;default:''"`
	Summary     string `gorm:"type:text;not null"`
	Description string `gorm:"type:text;not null"`
	Category    string `gorm:"size:64;not null;default:'其他'"`
	Developer   string `gorm:"size:255;not null;default:''"`
	Icon        string `gorm:"size:255;not null;default:''"`
	Screenshots string `gorm:"type:text;not null"`
	Featured    bool   `gorm:"not null;default:false"`
	Published   bool   `gorm:"not null;default:true"`
	Downloads   int64  `gorm:"not null;default:0"`
	CreatedAt   int64  `gorm:"not null;autoCreateTime:false"`
	UpdatedAt   int64  `gorm:"not null;autoUpdateTime:false"`
}

func (StoreApp) TableName() string { return "apps" }

// Version is a downloadable build of a StoreApp.
type Version struct {
	ID        uint   `gorm:"primaryKey;autoIncrement"`
	AppID     string `gorm:"size:64;not null;index;uniqueIndex:uk_versions_app_version"`
	Version   string `gorm:"size:64;not null;uniqueIndex:uk_versions_app_version"`
	Changelog string `gorm:"type:text;not null"`
	Filename  string `gorm:"size:255;not null;default:''"`
	Size      int64  `gorm:"not null;default:0"`
	SHA256    string `gorm:"size:64;not null;default:''"`
	CreatedAt int64  `gorm:"not null;autoCreateTime:false"`
}

func (Version) TableName() string { return "versions" }

// MiniApp is an NXKit mini program; its package payload lives on disk under
// <DATA_DIR>/miniapps/<id>/package/.
type MiniApp struct {
	ID          string `gorm:"primaryKey;size:64"`
	Title       string `gorm:"size:255;not null;default:''"`
	Summary     string `gorm:"type:text;not null"`
	Description string `gorm:"type:text;not null"`
	Category    string `gorm:"size:64;not null;default:'其他'"`
	Developer   string `gorm:"size:255;not null;default:''"`
	Version     string `gorm:"size:64;not null;default:'1.0.0'"`
	Icon        string `gorm:"size:255;not null;default:''"`
	Published   bool   `gorm:"not null;default:true"`
	Downloads   int64  `gorm:"not null;default:0"`
	CreatedAt   int64  `gorm:"not null;autoCreateTime:false"`
	UpdatedAt   int64  `gorm:"not null;autoUpdateTime:false"`
}

func (MiniApp) TableName() string { return "miniapps" }

// DocPage 是文档站的一个页面；Content 为主内容区 HTML（不含页面骨架）。
type DocPage struct {
	Path      string `gorm:"primaryKey;size:128"`
	Title     string `gorm:"size:255;not null;default:''"`
	Meta      string `gorm:"size:255;not null;default:''"`
	Content   string `gorm:"type:longtext;not null"`
	UpdatedAt int64  `gorm:"not null;autoUpdateTime:false"`
}

func (DocPage) TableName() string { return "doc_pages" }
