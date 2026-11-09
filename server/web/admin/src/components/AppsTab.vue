<template>
  <section>
    <div class="toolbar">
      <input
        v-model="q"
        class="search"
        placeholder="搜索标题 / ID / 开发者"
        @keyup.enter="reload(1)"
      />
      <button class="btn" @click="reload(1)">搜索</button>
      <span class="spacer"></span>
      <button class="btn primary" @click="openCreate">新建应用</button>
    </div>
    <p v-if="error" class="error">{{ error }}</p>

    <div class="card">
      <table class="table">
        <thead>
          <tr>
            <th>图标</th>
            <th>标题</th>
            <th>ID</th>
            <th>分类</th>
            <th>来源</th>
            <th>下载</th>
            <th>发布</th>
            <th>精选</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="app in items" :key="app.id">
            <td>
              <img v-if="app.icon_url" :src="app.icon_url" class="icon" alt="" />
              <div v-else class="icon placeholder">{{ (app.title || '?').slice(0, 1) }}</div>
            </td>
            <td>{{ app.title }}</td>
            <td class="mono">{{ app.id }}</td>
            <td>{{ app.category }}</td>
            <td>{{ app.source === 'container' ? '容器版本' : '上传' }}</td>
            <td>{{ app.downloads }}</td>
            <td>
              <label class="switch">
                <input
                  type="checkbox"
                  :checked="app.published"
                  @change="toggleFlag(app, 'published', $event.target.checked)"
                />
                <span></span>
              </label>
            </td>
            <td>
              <label class="switch">
                <input
                  type="checkbox"
                  :checked="app.featured"
                  @change="toggleFlag(app, 'featured', $event.target.checked)"
                />
                <span></span>
              </label>
            </td>
            <td><button class="btn small" @click="openDetail(app)">管理</button></td>
          </tr>
          <tr v-if="!items.length">
            <td colspan="9" class="empty">暂无数据</td>
          </tr>
        </tbody>
      </table>
      <div class="pager" v-if="pages > 1">
        <button class="btn small" :disabled="page <= 1" @click="reload(page - 1)">上一页</button>
        <span class="muted">{{ page }} / {{ pages }}</span>
        <button class="btn small" :disabled="page >= pages" @click="reload(page + 1)">下一页</button>
      </div>
    </div>

    <div v-if="showCreate" class="modal-mask" @click.self="showCreate = false">
      <form class="modal card" @submit.prevent="createApp">
        <h2>新建应用</h2>
        <label>应用 ID<input v-model="createForm.id" placeholder="例如 my-app" required /></label>
        <label>标题<input v-model="createForm.title" required /></label>
        <label>简介<input v-model="createForm.summary" /></label>
        <label>详细描述<textarea v-model="createForm.description" rows="3"></textarea></label>
        <label>分类<input v-model="createForm.category" placeholder="默认：其他" /></label>
        <label>开发者<input v-model="createForm.developer" /></label>
        <label>下载来源
          <select v-model="createForm.source">
            <option value="upload">上传 NRO 版本</option>
            <option value="container">跟随容器最新版本</option>
          </select>
        </label>
        <div class="row">
          <label class="check"><input type="checkbox" v-model="createForm.published" /> 立即发布</label>
          <label class="check"><input type="checkbox" v-model="createForm.featured" /> 精选</label>
        </div>
        <p v-if="createError" class="error">{{ createError }}</p>
        <div class="actions">
          <button type="button" class="btn ghost" @click="showCreate = false">取消</button>
          <button class="btn primary" :disabled="creating">{{ creating ? '创建中…' : '创建' }}</button>
        </div>
      </form>
    </div>

    <div v-if="detail" class="drawer-mask" @click.self="closeDetail">
      <div class="drawer">
        <header class="drawer-header">
          <h2>{{ detail.title }}</h2>
          <button class="btn ghost" @click="closeDetail">关闭</button>
        </header>
        <p v-if="drawerError" class="error">{{ drawerError }}</p>

        <form class="card section" @submit.prevent="saveInfo">
          <h3>基本信息</h3>
          <label>标题<input v-model="infoForm.title" required /></label>
          <label>简介<input v-model="infoForm.summary" /></label>
          <label>详细描述<textarea v-model="infoForm.description" rows="4"></textarea></label>
          <label>分类<input v-model="infoForm.category" /></label>
          <label>开发者<input v-model="infoForm.developer" /></label>
          <label>下载来源
            <select v-model="infoForm.source">
              <option value="upload">上传 NRO 版本</option>
              <option value="container">跟随容器最新版本</option>
            </select>
          </label>
          <p v-if="infoForm.source === 'container'" class="muted">
            该应用直接下发「容器版本」中最新已发布的 NRO，无需在此单独上传。
          </p>
          <button class="btn primary" :disabled="savingInfo">{{ savingInfo ? '保存中…' : '保存' }}</button>
        </form>

        <div class="card section">
          <h3>图标</h3>
          <div class="row">
            <img v-if="detail.icon_url" :src="detail.icon_url" class="icon large" alt="" />
            <div v-else class="icon large placeholder">{{ (detail.title || '?').slice(0, 1) }}</div>
            <input ref="iconInput" type="file" accept=".png,.jpg,.jpeg,.webp" hidden @change="uploadIcon" />
            <button class="btn" @click="iconInput && iconInput.click()">上传新图标</button>
          </div>
        </div>

        <div class="card section">
          <h3>截图（{{ (detail.screenshots || []).length }}/10）</h3>
          <div class="shots">
            <div v-for="shot in detail.screenshots" :key="shot" class="shot">
              <img :src="shot" alt="" />
              <button class="btn small danger" @click="removeScreenshot(shot)">删除</button>
            </div>
          </div>
          <div>
            <input
              ref="shotInput"
              type="file"
              accept=".png,.jpg,.jpeg,.webp"
              multiple
              hidden
              @change="uploadScreenshots"
            />
            <button
              class="btn"
              :disabled="(detail.screenshots || []).length >= 10"
              @click="shotInput && shotInput.click()"
            >上传截图</button>
          </div>
        </div>

        <div v-if="detail.source !== 'container'" class="card section">
          <h3>版本</h3>
          <table class="table">
            <thead>
              <tr>
                <th>版本</th>
                <th>大小</th>
                <th>上传时间</th>
                <th></th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="v in detail.versions || []" :key="v.id">
                <td class="mono">{{ v.version }}</td>
                <td>{{ formatBytes(v.size) }}</td>
                <td class="muted">{{ formatTime(v.created_at) }}</td>
                <td><button class="btn small danger" @click="removeVersion(v)">删除</button></td>
              </tr>
              <tr v-if="!(detail.versions || []).length">
                <td colspan="4" class="empty">尚未上传版本</td>
              </tr>
            </tbody>
          </table>
          <form class="version-form" @submit.prevent="uploadVersion">
            <input v-model="versionForm.version" placeholder="版本号，如 1.0.0" required />
            <input v-model="versionForm.changelog" placeholder="更新日志（可选）" />
            <input
              ref="versionFileInput"
              type="file"
              accept=".nro"
              hidden
              @change="onVersionFile"
            />
            <div class="row">
              <button type="button" class="btn" @click="versionFileInput && versionFileInput.click()">
                {{ versionForm.file ? versionForm.file.name : '选择 .nro 文件' }}
              </button>
              <button class="btn primary" :disabled="!versionForm.file || uploadingVersion">
                {{ uploadingVersion ? '上传中…' : '上传版本' }}
              </button>
            </div>
          </form>
        </div>

        <div class="card section danger-zone">
          <h3>危险操作</h3>
          <div>
            <button class="btn danger" @click="removeApp">删除该应用</button>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { api, formatBytes, formatTime } from '../api'

const items = ref([])
const total = ref(0)
const page = ref(1)
const pageSize = 50
const q = ref('')
const error = ref('')

const pages = computed(() => Math.max(1, Math.ceil(total.value / pageSize)))

const showCreate = ref(false)
const creating = ref(false)
const createError = ref('')
const createForm = ref(emptyCreateForm())

const detail = ref(null)
const drawerError = ref('')
const infoForm = ref({})
const savingInfo = ref(false)
const iconInput = ref(null)
const shotInput = ref(null)
const versionFileInput = ref(null)
const versionForm = ref({ version: '', changelog: '', file: null })
const uploadingVersion = ref(false)

function emptyCreateForm() {
  return {
    id: '',
    title: '',
    summary: '',
    description: '',
    category: '',
    developer: '',
    source: 'upload',
    featured: false,
    published: true
  }
}

function openCreate() {
  createForm.value = emptyCreateForm()
  createError.value = ''
  showCreate.value = true
}

async function reload(target) {
  error.value = ''
  const p = target || page.value
  try {
    const data = await api.get(
      `/api/v1/admin/apps?q=${encodeURIComponent(q.value.trim())}&page=${p}&page_size=${pageSize}`
    )
    items.value = data.items || []
    total.value = data.total || 0
    page.value = data.page || p
  } catch (err) {
    error.value = err.message || '加载失败'
  }
}

async function toggleFlag(app, field, value) {
  try {
    const updated = await api.patch(`/api/v1/admin/apps/${app.id}/flags`, { [field]: value })
    Object.assign(app, updated)
  } catch (err) {
    error.value = err.message || '操作失败'
  }
}

async function createApp() {
  createError.value = ''
  creating.value = true
  try {
    const form = createForm.value
    const created = await api.post('/api/v1/admin/apps', {
      id: form.id.trim(),
      title: form.title.trim(),
      summary: form.summary.trim(),
      description: form.description,
      category: form.category.trim(),
      developer: form.developer.trim(),
      source: form.source,
      featured: form.featured,
      published: form.published
    })
    showCreate.value = false
    await reload(1)
    // 创建后直接打开详情，方便接着上传图标。
    if (created && created.id) await openDetail(created)
  } catch (err) {
    createError.value = err.message || '创建失败'
  } finally {
    creating.value = false
  }
}

async function openDetail(app) {
  drawerError.value = ''
  versionForm.value = { version: '', changelog: '', file: null }
  try {
    const data = await api.get(`/api/v1/admin/apps/${app.id}`)
    detail.value = data
    infoForm.value = {
      title: data.title,
      summary: data.summary,
      description: data.description,
      category: data.category,
      developer: data.developer,
      source: data.source || 'upload'
    }
  } catch (err) {
    error.value = err.message || '加载失败'
  }
}

function closeDetail() {
  detail.value = null
}

function applyDetail(data) {
  detail.value = data
  const idx = items.value.findIndex((a) => a.id === data.id)
  if (idx >= 0) {
    items.value.splice(idx, 1, data)
  }
}

async function refreshDetail() {
  const data = await api.get(`/api/v1/admin/apps/${detail.value.id}`)
  applyDetail(data)
}

async function saveInfo() {
  savingInfo.value = true
  drawerError.value = ''
  try {
    const form = infoForm.value
    const updated = await api.put(`/api/v1/admin/apps/${detail.value.id}`, {
      title: form.title,
      summary: form.summary,
      description: form.description,
      category: form.category,
      developer: form.developer,
      source: form.source
    })
    applyDetail(Object.assign({}, detail.value, updated))
    await refreshDetail()
  } catch (err) {
    drawerError.value = err.message || '保存失败'
  } finally {
    savingInfo.value = false
  }
}

async function uploadIcon(event) {
  const file = event.target.files && event.target.files[0]
  event.target.value = ''
  if (!file) return
  drawerError.value = ''
  const form = new FormData()
  form.append('file', file)
  try {
    const updated = await api.postForm(`/api/v1/admin/apps/${detail.value.id}/icon`, form)
    applyDetail(Object.assign({}, detail.value, updated))
    await refreshDetail()
  } catch (err) {
    drawerError.value = err.message || '上传失败'
  }
}

async function uploadScreenshots(event) {
  const files = Array.from(event.target.files || [])
  event.target.value = ''
  if (!files.length) return
  drawerError.value = ''
  const form = new FormData()
  files.forEach((f) => form.append('files', f))
  try {
    const updated = await api.postForm(`/api/v1/admin/apps/${detail.value.id}/screenshots`, form)
    applyDetail(Object.assign({}, detail.value, updated))
    await refreshDetail()
  } catch (err) {
    drawerError.value = err.message || '上传失败'
  }
}

async function removeScreenshot(url) {
  const filename = String(url).split('/').pop()
  drawerError.value = ''
  try {
    await api.post(`/api/v1/admin/apps/${detail.value.id}/screenshots/delete`, { filename })
    await refreshDetail()
  } catch (err) {
    drawerError.value = err.message || '删除失败'
  }
}

function onVersionFile(event) {
  versionForm.value.file = (event.target.files && event.target.files[0]) || null
}

async function uploadVersion() {
  const form = versionForm.value
  if (!form.file) return
  uploadingVersion.value = true
  drawerError.value = ''
  const data = new FormData()
  data.append('file', form.file)
  data.append('version', form.version.trim())
  data.append('changelog', form.changelog)
  try {
    await api.postForm(`/api/v1/admin/apps/${detail.value.id}/versions`, data)
    versionForm.value = { version: '', changelog: '', file: null }
    await refreshDetail()
  } catch (err) {
    drawerError.value = err.message || '上传失败'
  } finally {
    uploadingVersion.value = false
  }
}

async function removeVersion(v) {
  if (!window.confirm(`确定删除版本 ${v.version} 吗？`)) return
  drawerError.value = ''
  try {
    await api.del(`/api/v1/admin/apps/${detail.value.id}/versions/${v.id}`)
    await refreshDetail()
  } catch (err) {
    drawerError.value = err.message || '删除失败'
  }
}

async function removeApp() {
  if (!window.confirm(`确定删除应用 ${detail.value.id} 吗？该操作不可恢复。`)) return
  try {
    await api.del(`/api/v1/admin/apps/${detail.value.id}`)
    closeDetail()
    await reload()
  } catch (err) {
    drawerError.value = err.message || '删除失败'
  }
}

onMounted(() => reload(1))
</script>
