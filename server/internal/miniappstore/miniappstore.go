// Package miniappstore implements the on-disk layout of NXKit mini programs
// and the ZIP_STORED packaging the Switch client expects.
package miniappstore

import (
	"archive/zip"
	"encoding/json"
	"errors"
	"fmt"
	"hash/crc32"
	"io"
	"io/fs"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"

	"gorm.io/gorm"

	"nxminiapp/server/internal/models"
)

var idPattern = regexp.MustCompile(`^[a-z0-9][a-z0-9_-]{1,63}$`)

// MiniAppDir is the root directory of a mini program.
func MiniAppDir(dataDir, id string) string {
	return filepath.Join(dataDir, "miniapps", id)
}

// PackageDir is where the unpacked payload (app.json, pages/...) lives.
func PackageDir(dataDir, id string) string {
	return filepath.Join(MiniAppDir(dataDir, id), "package")
}

func fileExists(p string) bool {
	info, err := os.Stat(p)
	return err == nil && !info.IsDir()
}

// DirSize returns the total size in bytes of all regular files under root.
func DirSize(root string) int64 {
	var total int64
	_ = filepath.WalkDir(root, func(_ string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if !d.IsDir() {
			if info, ierr := d.Info(); ierr == nil {
				total += info.Size()
			}
		}
		return nil
	})
	return total
}

func isASCII(s string) bool {
	for i := 0; i < len(s); i++ {
		if s[i] >= 0x80 {
			return false
		}
	}
	return true
}

// WriteStoredZip writes every file under root into w as a ZIP archive whose
// entries are stored uncompressed (ZIP_STORED) with no data descriptors.
//
// The Switch client refuses entries carrying the data-descriptor flag (bit 3).
// archive/zip's CreateHeader always sets that flag, so entries are written via
// CreateRaw with Method=Store and CRC32/CompressedSize64/UncompressedSize64
// pre-filled: the local file header then carries the real values and no data
// descriptor is emitted.
func WriteStoredZip(w io.Writer, root string) error {
	names := make([]string, 0)
	err := filepath.WalkDir(root, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}
		rel, err := filepath.Rel(root, p)
		if err != nil {
			return err
		}
		names = append(names, filepath.ToSlash(rel))
		return nil
	})
	if err != nil {
		return err
	}
	sort.Strings(names)

	zw := zip.NewWriter(w)
	for _, name := range names {
		full := filepath.Join(root, filepath.FromSlash(name))
		info, err := os.Stat(full)
		if err != nil {
			_ = zw.Close()
			return err
		}
		data, err := os.ReadFile(full)
		if err != nil {
			_ = zw.Close()
			return err
		}
		hdr := &zip.FileHeader{
			Name:               name,
			Method:             zip.Store,
			ReaderVersion:      20,
			CRC32:              crc32.ChecksumIEEE(data),
			CompressedSize64:   uint64(len(data)),
			UncompressedSize64: uint64(len(data)),
		}
		hdr.SetModTime(info.ModTime())
		if !isASCII(name) {
			hdr.Flags |= 0x800 // UTF-8 encoded entry name
		}
		entry, err := zw.CreateRaw(hdr)
		if err != nil {
			_ = zw.Close()
			return err
		}
		if _, err := entry.Write(data); err != nil {
			_ = zw.Close()
			return err
		}
	}
	return zw.Close()
}

// ExtractPackage replaces destDir with the contents of the zip archive at
// zipPath. Entries with absolute paths or ".." segments are rejected, and the
// archive must contain app.json at its root.
func ExtractPackage(zipPath, destDir string) error {
	zr, err := zip.OpenReader(zipPath)
	if err != nil {
		return errors.New("无法读取压缩包，文件可能已损坏")
	}
	defer zr.Close()

	type entry struct {
		file *zip.File
		name string
	}
	entries := make([]entry, 0, len(zr.File))
	hasAppJSON := false
	for _, f := range zr.File {
		name := strings.ReplaceAll(f.Name, "\\", "/")
		if strings.HasPrefix(name, "/") || (len(name) > 1 && name[1] == ':') {
			return fmt.Errorf("包内包含非法路径: %s", f.Name)
		}
		cleaned := path.Clean(name)
		if cleaned == ".." || strings.HasPrefix(cleaned, "../") {
			return fmt.Errorf("包内包含非法路径: %s", f.Name)
		}
		if strings.HasSuffix(name, "/") || f.FileInfo().IsDir() {
			continue
		}
		if cleaned == "app.json" {
			hasAppJSON = true
		}
		entries = append(entries, entry{file: f, name: cleaned})
	}
	if !hasAppJSON {
		return errors.New("包内缺少 app.json")
	}

	parent := filepath.Dir(destDir)
	if err := os.MkdirAll(parent, 0o755); err != nil {
		return err
	}
	tmp, err := os.MkdirTemp(parent, ".pkg-*")
	if err != nil {
		return err
	}
	ok := false
	defer func() {
		if !ok {
			_ = os.RemoveAll(tmp)
		}
	}()

	for _, e := range entries {
		target := filepath.Join(tmp, filepath.FromSlash(e.name))
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return err
		}
		rc, err := e.file.Open()
		if err != nil {
			return err
		}
		out, err := os.OpenFile(target, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
		if err != nil {
			_ = rc.Close()
			return err
		}
		_, copyErr := io.Copy(out, rc)
		closeErr := out.Close()
		_ = rc.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeErr != nil {
			return closeErr
		}
	}

	if err := os.RemoveAll(destDir); err != nil {
		return err
	}
	if err := os.Rename(tmp, destDir); err != nil {
		return err
	}
	ok = true
	return nil
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	out, err := os.OpenFile(dst, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		return err
	}
	_, copyErr := io.Copy(out, in)
	closeErr := out.Close()
	if copyErr != nil {
		return copyErr
	}
	return closeErr
}

// copyPackage copies the payload of a seed directory (everything except the
// top-level meta.json and icon.png) into dst.
func copyPackage(src, dst string) error {
	return filepath.WalkDir(src, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		rel, err := filepath.Rel(src, p)
		if err != nil {
			return err
		}
		if rel == "." {
			return nil
		}
		if d.IsDir() {
			return os.MkdirAll(filepath.Join(dst, rel), 0o755)
		}
		if !strings.ContainsRune(rel, filepath.Separator) && (rel == "meta.json" || rel == "icon.png") {
			return nil
		}
		return copyFile(p, filepath.Join(dst, rel))
	})
}

func metaString(meta map[string]any, key, fallback string) string {
	if value, ok := meta[key].(string); ok && strings.TrimSpace(value) != "" {
		return strings.TrimSpace(value)
	}
	return fallback
}

func metaBool(meta map[string]any, key string, fallback bool) bool {
	if value, ok := meta[key].(bool); ok {
		return value
	}
	return fallback
}

// HomeID 是内置首页小程序的 ID。
const HomeID = "home"

// settingHomeMiniApp 与 api.SettingHomeMiniApp 保持一致（避免循环依赖）。
const settingHomeMiniApp = "home.miniapp_id"

// Seed imports bundled mini programs from seedDir (the image ships them at
// /seed/miniapps) when the miniapps table is still empty. Each seed directory
// must contain app.json; meta.json provides the catalogue row and icon.png is
// stored next to the package directory.
//
// 内置首页（HomeID）会单独补齐：即使库里已有其他小程序，缺少 home 时也会导入，
// 并在尚未配置首页小程序时写入默认值。
func Seed(db *gorm.DB, dataDir, seedDir string) error {
	var count int64
	if err := db.Model(&models.MiniApp{}).Count(&count).Error; err != nil {
		return err
	}
	entries, err := os.ReadDir(seedDir)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		id := entry.Name()
		if !idPattern.MatchString(id) {
			continue
		}
		if count > 0 && id != HomeID {
			continue
		}
		src := filepath.Join(seedDir, id)
		if !fileExists(filepath.Join(src, "app.json")) {
			continue
		}

		var existing models.MiniApp
		if err := db.Where("id = ?", id).Take(&existing).Error; err == nil {
			continue
		}

		meta := map[string]any{}
		if raw, err := os.ReadFile(filepath.Join(src, "meta.json")); err == nil {
			_ = json.Unmarshal(raw, &meta)
		}

		dst := MiniAppDir(dataDir, id)
		if err := os.RemoveAll(dst); err != nil {
			return err
		}
		if err := os.MkdirAll(dst, 0o755); err != nil {
			return err
		}
		if err := copyPackage(src, PackageDir(dataDir, id)); err != nil {
			return err
		}

		icon := ""
		iconSrc := filepath.Join(src, "icon.png")
		if fileExists(iconSrc) {
			if err := copyFile(iconSrc, filepath.Join(dst, "icon.png")); err != nil {
				return err
			}
			icon = "icon.png"
		}

		now := time.Now().UnixMilli()
		row := models.MiniApp{
			ID:          id,
			Title:       metaString(meta, "title", id),
			Summary:     metaString(meta, "summary", ""),
			Description: metaString(meta, "description", ""),
			Category:    metaString(meta, "category", "示例"),
			Developer:   metaString(meta, "developer", ""),
			Version:     metaString(meta, "version", "1.0.0"),
			Icon:        icon,
			Published:   metaBool(meta, "published", true),
			Downloads:   0,
			CreatedAt:   now,
			UpdatedAt:   now,
		}
		if err := db.Create(&row).Error; err != nil {
			return err
		}
	}

	// 首次部署时把内置 home 设为首页；后台改过配置后不会被覆盖。
	var home models.MiniApp
	if err := db.Where("id = ?", HomeID).Take(&home).Error; err != nil {
		return nil
	}
	var setting models.Setting
	if err := db.Where("`key` = ?", settingHomeMiniApp).Take(&setting).Error; err == nil {
		return nil
	}
	return db.Create(&models.Setting{Key: settingHomeMiniApp, Value: HomeID}).Error
}
