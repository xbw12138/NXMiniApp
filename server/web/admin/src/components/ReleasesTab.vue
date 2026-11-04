<template>
  <section>
    <div class="toolbar">
      <span class="muted">共 {{ items.length }} 个容器版本</span>
      <span class="spacer"></span>
      <button class="btn primary" @click="openUpload">上传新版本</button>
    </div>
    <p v-if="error" class="error">{{ error }}</p>

    <div class="card">
      <table class="table">
        <thead>
          <tr>
            <th>版本</th>
            <th>大小</th>
            <th>SHA-256</th>
            <th>下载量</th>
            <th>上传时间</th>
            <th>发布</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="r in items" :key="r.id">
            <td>
              <span class="mono strong">v{{ r.version }}</span>
              <span v-if="isLatest(r)" class="badge">最新</span>
              <div v-if="r.notes" class="muted small-text">{{ r.notes }}</div>
            </td>
            <td>{{ formatBytes(r.size) }}</td>
            <td class="mono muted">{{ (r.sha256 || '').slice(0, 12) }}…</td>
            <td>{{ r.downloads || 0 }}</td>
            <td>{{ formatTime(r.created_at) }}</td>
            <td>
              <label class="switch">
                <input
                  type="checkbox"
                  :checked="r.published"
                  @change="toggleFlag(r, $event.target.checked)"
                />
                <span></span>
              </label>
            </td>
            <td class="actions-cell">
              <a class="btn small" :href="r.download_url">下载</a>
              <button class="btn small danger" @click="removeRelease(r)">删除</button>
            </td>
          </tr>
          <tr v-if="!items.length">
            <td colspan="7" class="empty">暂无容器版本，点击右上角上传</td>
          </tr>
        </tbody>
      </table>
    </div>

    <div v-if="showUpload" class="modal-mask" @click.self="showUpload = false">
      <form class="modal card" @submit.prevent="uploadRelease">
        <h2>上传容器版本</h2>
        <label>版本号<input v-model="form.version" placeholder="例如 0.3.0" required /></label>
        <label>更新说明<textarea v-model="form.notes" rows="3" placeholder="本版本的变更内容（可选）"></textarea></label>
        <label>容器文件（.nro，≤ 128MB）
          <input ref="fileInput" type="file" accept=".nro" @change="onFile" required />
        </label>
        <div class="row">
          <label class="check"><input type="checkbox" v-model="form.published" /> 立即发布</label>
        </div>
        <p v-if="uploadError" class="error">{{ uploadError }}</p>
        <div class="actions">
          <button type="button" class="btn ghost" @click="showUpload = false">取消</button>
          <button class="btn primary" :disabled="uploading">{{ uploading ? '上传中…' : '上传' }}</button>
        </div>
      </form>
    </div>
  </section>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import { api, formatBytes, formatTime } from '../api'

const items = ref([])
const error = ref('')

const showUpload = ref(false)
const uploading = ref(false)
const uploadError = ref('')
const form = ref({ version: '', notes: '', published: true, file: null })
const fileInput = ref(null)

function isLatest(r) {
  return items.value.length > 0 && items.value[0].id === r.id
}

function openUpload() {
  form.value = { version: '', notes: '', published: true, file: null }
  uploadError.value = ''
  showUpload.value = true
}

function onFile(event) {
  form.value.file = (event.target.files && event.target.files[0]) || null
}

async function reload() {
  error.value = ''
  try {
    const data = await api.get('/api/v1/admin/releases')
    items.value = data.items || []
  } catch (err) {
    error.value = err.message || '加载失败'
  }
}

function applyItem(data) {
  const idx = items.value.findIndex((r) => r.id === data.id)
  if (idx >= 0) {
    items.value.splice(idx, 1, data)
  }
}

async function toggleFlag(r, value) {
  try {
    const updated = await api.patch(`/api/v1/admin/releases/${r.id}/flags`, { published: value })
    applyItem(updated)
  } catch (err) {
    error.value = err.message || '操作失败'
  }
}

async function uploadRelease() {
  uploadError.value = ''
  if (!form.value.file) {
    uploadError.value = '请选择 .nro 文件'
    return
  }
  if (form.value.file.size > 128 * 1024 * 1024) {
    uploadError.value = '文件不能超过 128MB'
    return
  }
  uploading.value = true
  const data = new FormData()
  data.append('version', form.value.version.trim())
  data.append('notes', form.value.notes)
  data.append('published', form.value.published ? 'true' : 'false')
  data.append('file', form.value.file)
  try {
    await api.postForm('/api/v1/admin/releases', data)
    showUpload.value = false
    await reload()
  } catch (err) {
    uploadError.value = err.message || '上传失败'
  } finally {
    uploading.value = false
  }
}

async function removeRelease(r) {
  if (!window.confirm(`确定删除容器版本 v${r.version} 吗？该操作不可恢复。`)) return
  try {
    await api.del(`/api/v1/admin/releases/${r.id}`)
    await reload()
  } catch (err) {
    error.value = err.message || '删除失败'
  }
}

onMounted(reload)
</script>

<style scoped>
.badge {
  margin-left: 8px;
  padding: 1px 8px;
  border-radius: 999px;
  background: #e6f7ee;
  color: #067a3f;
  font-size: 11px;
  font-weight: 600;
}
.strong { font-weight: 600; }
.small-text {
  margin-top: 3px;
  font-size: 12px;
  max-width: 260px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.actions-cell { white-space: nowrap; }
.actions-cell .btn + .btn { margin-left: 6px; }
</style>
