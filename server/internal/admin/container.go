package admin

import (
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"

	"nxminiapp/server/internal/api"
	"nxminiapp/server/internal/models"
)

// 容器 NRO 大小上限（nro 通常几十 MB，留出余量）。
const maxReleaseSize = 128 << 20

func listReleases(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		rows := make([]models.ContainerRelease, 0)
		e.DB.Order("created_at DESC, id DESC").Find(&rows)
		items := make([]gin.H, 0, len(rows))
		for i := range rows {
			items = append(items, api.ReleaseJSON(&rows[i]))
		}
		c.JSON(http.StatusOK, gin.H{"items": items})
	}
}

// uploadRelease 上传一个容器 NRO 版本：multipart(version, notes, published?, file)。
func uploadRelease(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		version := strings.TrimSpace(c.PostForm("version"))
		if version == "" {
			api.Abort(c, http.StatusBadRequest, "版本号不能为空")
			return
		}
		if len(version) > 64 {
			api.Abort(c, http.StatusBadRequest, "版本号过长")
			return
		}
		var count int64
		e.DB.Model(&models.ContainerRelease{}).Where("version = ?", version).Count(&count)
		if count > 0 {
			api.Abort(c, http.StatusConflict, "该版本号已存在")
			return
		}
		file, err := c.FormFile("file")
		if err != nil {
			api.Abort(c, http.StatusBadRequest, "请选择 .nro 文件")
			return
		}
		if strings.ToLower(filepath.Ext(file.Filename)) != ".nro" {
			api.Abort(c, http.StatusBadRequest, "容器包必须是 .nro 文件")
			return
		}
		if file.Size <= 0 || file.Size > maxReleaseSize {
			api.Abort(c, http.StatusBadRequest, "容器包大小需在 128MB 以内")
			return
		}
		// 默认上架；显式传 published=false 则先存为未发布。
		published := strings.ToLower(strings.TrimSpace(c.PostForm("published"))) != "false"

		name := fmt.Sprintf("nxminiapp-%s-%s.nro", api.SlugFor(version), api.RandomHex(4))
		dst := filepath.Join(e.Cfg.ReleasesDir(), name)
		if err := c.SaveUploadedFile(file, dst); err != nil {
			api.Abort(c, http.StatusInternalServerError, "文件保存失败")
			return
		}
		info, err := os.Stat(dst)
		if err != nil {
			api.Abort(c, http.StatusInternalServerError, "文件保存失败")
			return
		}
		digest, err := api.Sha256File(dst)
		if err != nil {
			_ = os.Remove(dst)
			api.Abort(c, http.StatusInternalServerError, "文件校验失败")
			return
		}

		// 用 map 显式插入，保证 published=false 也会写入。
		record := map[string]interface{}{
			"version":    version,
			"notes":      strings.TrimSpace(c.PostForm("notes")),
			"filename":   name,
			"size":       info.Size(),
			"sha256":     digest,
			"downloads":  0,
			"published":  published,
			"created_at": api.NowMS(),
		}
		if err := e.DB.Model(&models.ContainerRelease{}).Create(record).Error; err != nil {
			_ = os.Remove(dst)
			api.Abort(c, http.StatusInternalServerError, "版本创建失败")
			return
		}
		var row models.ContainerRelease
		e.DB.Where("version = ?", version).Take(&row)
		c.JSON(http.StatusOK, api.ReleaseJSON(&row))
	}
}

func patchReleaseFlags(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		var body struct {
			Published *bool `json:"published"`
		}
		if err := c.ShouldBindJSON(&body); err != nil || body.Published == nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		id, err := strconv.ParseUint(c.Param("id"), 10, 64)
		if err != nil {
			api.Abort(c, http.StatusNotFound, "版本不存在")
			return
		}
		var r models.ContainerRelease
		if err := e.DB.Where("id = ?", id).Take(&r).Error; err != nil {
			api.Abort(c, http.StatusNotFound, "版本不存在")
			return
		}
		e.DB.Model(&models.ContainerRelease{}).Where("id = ?", r.ID).
			Update("published", *body.Published)
		r.Published = *body.Published
		c.JSON(http.StatusOK, api.ReleaseJSON(&r))
	}
}

func deleteRelease(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		id, err := strconv.ParseUint(c.Param("id"), 10, 64)
		if err != nil {
			api.Abort(c, http.StatusNotFound, "版本不存在")
			return
		}
		var r models.ContainerRelease
		if err := e.DB.Where("id = ?", id).Take(&r).Error; err != nil {
			api.Abort(c, http.StatusNotFound, "版本不存在")
			return
		}
		_ = os.Remove(filepath.Join(e.Cfg.ReleasesDir(), r.Filename))
		e.DB.Where("id = ?", r.ID).Delete(&models.ContainerRelease{})
		c.JSON(http.StatusOK, gin.H{"deleted": id})
	}
}
