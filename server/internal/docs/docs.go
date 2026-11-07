// Package docs 提供文档站的 seed 导入、页面渲染与后台管理接口。
//
// 文档内容存于 doc_pages 表（主内容区 HTML），页面骨架由本包统一渲染；
// 静态资源（assets/*.css、nav.js）直接从 DocsDir 目录提供。
// 首次启动且表为空时，从 DocsDir 的静态 HTML 解析入库作为初始内容。
package docs

import (
	"archive/zip"
	"bytes"
	"errors"
	"fmt"
	"html"
	"io/fs"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"

	"nxminiapp/server/internal/models"
)

const (
	titleOpen   = "<title>"
	titleClose  = "</title>"
	metaOpen    = "<span class=\"topbar-meta\">"
	metaClose   = "</span>"
	mainOpen    = "<main class=\"content\">"
	mainClose   = "</main>"
	pageSuffix  = ".html"
	contentType = "text/html; charset=utf-8"
)

// pageShell 复刻 docs 静态页的骨架；资源统一使用 /docs 绝对路径。
const pageShell = `<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s</title>
<link rel="stylesheet" href="/docs/assets/docs.css">
</head>
<body>
<header class="topbar">
  <a class="brand" href="/docs/index.html"><span class="logo">NX</span> 小程序开发文档</a>
  <span class="topbar-meta">%s</span>
</header>

<div class="layout">
  <aside class="sidebar" id="sidebar"></aside>

  <main class="content">%s
    <footer class="site-foot">
      <a href="/api/v1/docs/export" title="下载离线文档 zip">下载离线文档</a>
      <span class="dot">·</span>
      <a href="https://beian.miit.gov.cn/" target="_blank" rel="noopener noreferrer">鲁ICP备16004009号-2</a>
    </footer>
  </main>
</div>

<script src="/docs/assets/nav.js"></script>
</body>
</html>
`

// parsePage 从静态 HTML 中抽取标题、顶部描述与主内容区 HTML。
func parsePage(raw string) (title, meta, content string, ok bool) {
	title, raw1, found1 := cut(raw, titleOpen, titleClose)
	meta, raw2, found2 := cut(raw, metaOpen, metaClose)
	content, _, found3 := cut(raw, mainOpen, mainClose)
	if !found1 || !found2 || !found3 {
		return "", "", "", false
	}
	_ = raw1
	_ = raw2
	return strings.TrimSpace(title), strings.TrimSpace(meta), strings.TrimSpace(content), true
}

func cut(s, open, close string) (mid, rest string, found bool) {
	i := strings.Index(s, open)
	if i < 0 {
		return "", s, false
	}
	s = s[i+len(open):]
	j := strings.Index(s, close)
	if j < 0 {
		return "", s, false
	}
	return s[:j], s[j+len(close):], true
}

// loadSeedPage 读取并解析 DocsDir 下的单个页面文件。
func loadSeedPage(docsDir, path string) (*models.DocPage, error) {
	raw, err := os.ReadFile(filepath.Join(docsDir, filepath.FromSlash(path+pageSuffix)))
	if err != nil {
		return nil, err
	}
	title, meta, content, ok := parsePage(string(raw))
	if !ok {
		return nil, errInvalid
	}
	return &models.DocPage{Path: path, Title: title, Meta: meta, Content: content}, nil
}

var errInvalid = errors.New("文档页面结构无法解析")

// Seed 在 doc_pages 表为空时，将 DocsDir 下所有 .html 页面解析入库。
func Seed(db *gorm.DB, docsDir string) error {
	var count int64
	if err := db.Model(&models.DocPage{}).Count(&count).Error; err != nil {
		return err
	}
	if count > 0 {
		return nil
	}
	info, err := os.Stat(docsDir)
	if err != nil || !info.IsDir() {
		return nil // 无 seed 目录则跳过（例如本地开发未配置）
	}
	now := time.Now().UnixMilli()
	return filepath.WalkDir(docsDir, func(full string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() || !strings.HasSuffix(d.Name(), pageSuffix) {
			return nil
		}
		rel, err := filepath.Rel(docsDir, full)
		if err != nil {
			return nil
		}
		path := strings.TrimSuffix(filepath.ToSlash(rel), pageSuffix)
		page, err := loadSeedPage(docsDir, path)
		if err != nil {
			return nil // 跳过无法解析的文件
		}
		page.UpdatedAt = now
		return db.Create(page).Error
	})
}

// Register 挂载文档站公共路由：/docs、/docs/*（assets 静态文件 + 页面渲染）。
func Register(r *gin.Engine, db *gorm.DB, docsDir string) {
	// 离线文档导出：打包所有页面 + assets，解压后可直接浏览。
	r.GET("/api/v1/docs/export", func(c *gin.Context) {
		data, err := buildZip(db, docsDir, true)
		if err != nil {
			abort(c, http.StatusInternalServerError, "导出失败")
			return
		}
		c.Header("Content-Disposition", `attachment; filename="nxminiapp-docs.zip"`)
		c.Data(http.StatusOK, "application/zip", data)
	})

	r.GET("/docs", func(c *gin.Context) {
		c.Redirect(http.StatusMovedPermanently, "/docs/index.html")
	})
	r.GET("/docs/*filepath", func(c *gin.Context) {
		fp := c.Param("filepath") // 形如 /assets/docs.css 或 /guide/structure.html
		if fp == "" || fp == "/" {
			c.Redirect(http.StatusMovedPermanently, "/docs/index.html")
			return
		}
		if strings.HasPrefix(fp, "/assets/") {
			name := filepath.Clean(strings.TrimPrefix(fp, "/assets/"))
			if strings.HasPrefix(name, "..") {
				c.Status(http.StatusNotFound)
				return
			}
			c.File(filepath.Join(docsDir, "assets", name))
			return
		}
		if !strings.HasSuffix(fp, pageSuffix) {
			c.Status(http.StatusNotFound)
			return
		}
		path := strings.TrimSuffix(strings.TrimPrefix(fp, "/"), pageSuffix)
		var page models.DocPage
		if err := db.Where("path = ?", path).Take(&page).Error; err != nil {
			c.Data(http.StatusNotFound, contentType, []byte(render(
				"页面不存在 · NXMiniApp 小程序开发文档", "404",
				"<h1>页面不存在</h1><p class=\"lead\">该文档页面不存在或已被删除。</p>")))
			return
		}
		c.Data(http.StatusOK, contentType, []byte(render(page.Title, page.Meta, page.Content)))
	})
}

func render(title, meta, content string) string {
	return fmt.Sprintf(pageShell, html.EscapeString(title), html.EscapeString(meta), content)
}

// buildZip 将所有文档页面打包为 zip。
// offline=true 时把资源路径改为相对路径 assets/ 并把 assets 目录一并打进包，
// 解压后可直接离线浏览 index.html；
// offline=false 时输出 seed 格式（资源用 /docs/assets/ 绝对路径），可被 Seed 重新导入。
func buildZip(db *gorm.DB, docsDir string, offline bool) ([]byte, error) {
	rows := make([]models.DocPage, 0)
	if err := db.Order("path").Find(&rows).Error; err != nil {
		return nil, err
	}
	buf := &bytes.Buffer{}
	zw := zip.NewWriter(buf)
	for _, p := range rows {
		html := render(p.Title, p.Meta, p.Content)
		if offline {
			html = strings.ReplaceAll(html, "/docs/assets/", "assets/")
		}
		name := p.Path + pageSuffix
		fw, err := zw.Create(name)
		if err != nil {
			return nil, err
		}
		if _, err := fw.Write([]byte(html)); err != nil {
			return nil, err
		}
	}
	if offline {
		// 把 assets 目录下的静态资源也打包进来，保证离线可用。
		assetDir := filepath.Join(docsDir, "assets")
		filepath.WalkDir(assetDir, func(full string, d fs.DirEntry, err error) error {
			if err != nil || d.IsDir() {
				return nil
			}
			rel, err := filepath.Rel(docsDir, full)
			if err != nil {
				return nil
			}
			data, err := os.ReadFile(full)
			if err != nil {
				return nil
			}
			fw, err := zw.Create(filepath.ToSlash(rel))
			if err != nil {
				return nil
			}
			_, _ = fw.Write(data)
			return nil
		})
	}
	if err := zw.Close(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// RegisterAdmin 在已鉴权的 admin 分组上挂载文档管理接口。
func RegisterAdmin(g *gin.RouterGroup, db *gorm.DB, docsDir string) {
	g.GET("/docs", func(c *gin.Context) {
		rows := make([]models.DocPage, 0)
		db.Order("path").Find(&rows)
		items := make([]gin.H, 0, len(rows))
		for _, p := range rows {
			items = append(items, gin.H{
				"path":       p.Path,
				"title":      p.Title,
				"meta":       p.Meta,
				"updated_at": p.UpdatedAt,
				"url":        "/docs/" + p.Path + pageSuffix,
			})
		}
		c.JSON(http.StatusOK, gin.H{"items": items})
	})

	g.GET("/docs/page", func(c *gin.Context) {
		page := findPage(c, db, c.Query("path"))
		if page == nil {
			return
		}
		writePage(c, page)
	})

	g.PUT("/docs/page", func(c *gin.Context) {
		var body struct {
			Path    string `json:"path"`
			Title   string `json:"title"`
			Meta    string `json:"meta"`
			Content string `json:"content"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		page := findPage(c, db, body.Path)
		if page == nil {
			return
		}
		title := strings.TrimSpace(body.Title)
		if title == "" {
			abort(c, http.StatusBadRequest, "标题不能为空")
			return
		}
		db.Model(&models.DocPage{}).Where("path = ?", page.Path).Updates(map[string]any{
			"title":      title,
			"meta":       strings.TrimSpace(body.Meta),
			"content":    body.Content,
			"updated_at": time.Now().UnixMilli(),
		})
		db.Where("path = ?", page.Path).Take(page)
		writePage(c, page)
	})

	// 从 seed 文件恢复单页默认内容。
	g.POST("/docs/reset", func(c *gin.Context) {
		var body struct {
			Path string `json:"path"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		page := findPage(c, db, body.Path)
		if page == nil {
			return
		}
		seed, err := loadSeedPage(docsDir, page.Path)
		if err != nil {
			abort(c, http.StatusNotFound, "默认文档源不可用")
			return
		}
		db.Model(&models.DocPage{}).Where("path = ?", page.Path).Updates(map[string]any{
			"title":      seed.Title,
			"meta":       seed.Meta,
			"content":    seed.Content,
			"updated_at": time.Now().UnixMilli(),
		})
		db.Where("path = ?", page.Path).Take(page)
		writePage(c, page)
	})

	// 管理员备份导出：输出 seed 格式 HTML，可放入 DocsDir 重新导入。
	g.GET("/docs/export", func(c *gin.Context) {
		data, err := buildZip(db, docsDir, false)
		if err != nil {
			abort(c, http.StatusInternalServerError, "导出失败")
			return
		}
		c.Header("Content-Disposition", `attachment; filename="nxminiapp-docs-seed.zip"`)
		c.Data(http.StatusOK, "application/zip", data)
	})
}

func writePage(c *gin.Context, page *models.DocPage) {
	c.JSON(http.StatusOK, gin.H{
		"path":       page.Path,
		"title":      page.Title,
		"meta":       page.Meta,
		"content":    page.Content,
		"updated_at": page.UpdatedAt,
		"url":        "/docs/" + page.Path + pageSuffix,
	})
}

// findPage 按 path 取页面，失败时已写入响应并返回 nil。
func findPage(c *gin.Context, db *gorm.DB, path string) *models.DocPage {
	path = strings.TrimSuffix(strings.TrimSpace(path), pageSuffix)
	if path == "" {
		abort(c, http.StatusBadRequest, "缺少 path 参数")
		return nil
	}
	var page models.DocPage
	if err := db.Where("path = ?", path).Take(&page).Error; err != nil {
		abort(c, http.StatusNotFound, "文档页面不存在")
		return nil
	}
	return &page
}

func abort(c *gin.Context, status int, msg string) {
	c.AbortWithStatusJSON(status, gin.H{"detail": msg})
}
