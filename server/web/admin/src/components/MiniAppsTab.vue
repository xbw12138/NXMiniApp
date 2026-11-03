<template>
  <section>
    <div class="toolbar">
      <span class="muted">共 {{ items.length }} 个小程序</span>
      <span class="spacer"></span>
      <button class="btn primary" @click="openCreate">新建小程序</button>
    </div>
    <p v-if="error" class="error">{{ error }}</p>

    <div class="card">
      <table class="table">
        <thead>
          <tr>
            <th>图标</th>
            <th>标题</th>
            <th>ID</th>
            <th>版本</th>
            <th>大小</th>
            <th>发布</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="m in items" :key="m.id">
            <td>
              <img v-if="m.icon_url" :src="m.icon_url" class="icon" alt="" />
              <div v-else class="icon placeholder">{{ (m.title || '?').slice(0, 1) }}</div>
            </td>
            <td>{{ m.title }}</td>
            <td class="mono">{{ m.id }}</td>
            <td class="mono">{{ m.version }}</td>
            <td>{{ formatBytes(m.size) }}</td>
            <td>
              <label class="switch">
                <input
                  type="checkbox"
                  :checked="m.published"
                  @change="toggleFlag(m, $event.target.checked)"
                />
                <span></span>
              </label>
            </td>
            <td><button class="btn small" @click="openDetail(m)">管理</button></td>
          </tr>
          <tr v-if="!items.length">
            <td colspan="7" class="empty">暂无数据</td>
          </tr>
        </tbody>
      </table>
    </div>

    <div v-if="showCreate" class="modal-mask" @click.self="showCreate = false">
      <form class="modal card" @submit.prevent="createMiniApp">
        <h2>新建小程序</h2>
        <label>小程序 ID<input v-model="createForm.id" placeholder="例如 my-miniapp" required /></label>
        <label>标题<input v-model="createForm.title" required /></label>
        <label>简介<input v-model="createForm.summary" /></label>
        <label>详细描述<textarea v-model="createForm.description" rows="3"></textarea></label>
        <label>分类<input v-model="createForm.category" placeholder="默认：其他" /></label>
        <label>开发者<input v-model="createForm.developer" /></label>
        <label>版本号<input v-model="createForm.version" placeholder="默认：1.0.0" /></label>
        <div class="row">
          <label class="check"><input type="checkbox" v-model="createForm.published" /> 立即发布</label>
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
          <h3>元信息</h3>
          <label>标题<input v-model="infoForm.title" required /></label>
          <label>简介<input v-model="infoForm.summary" /></label>
          <label>详细描述<textarea v-model="infoForm.description" rows="4"></textarea></label>
          <label>分类<input v-model="infoForm.category" /></label>
          <label>开发者<input v-model="infoForm.developer" /></label>
          <label>版本号<input v-model="infoForm.version" placeholder="1.0.0" /></label>
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
          <h3>程序包</h3>
          <p class="muted">
            当前大小：{{ formatBytes(detail.size) }}。上传 .nxpkg / .zip 包将整体替换现有内容，包根目录必须包含 app.json。
          </p>
          <input v-model="packageForm.version" placeholder="新版本号（可选，留空则不修改）" />
          <input
            ref="packageFileInput"
            type="file"
            accept=".nxpkg,.zip"
            hidden
            @change="onPackageFile"
          />
          <div class="row">
            <button type="button" class="btn" @click="packageFileInput && packageFileInput.click()">
              {{ packageForm.file ? packageForm.file.name : '选择 .nxpkg / .zip 文件' }}
            </button>
            <button class="btn primary" :disabled="!packageForm.file || uploadingPackage" @click="uploadPackage">
              {{ uploadingPackage ? '上传中…' : '上传并替换' }}
            </button>
          </div>
        </div>

        <div class="card section danger-zone">
          <h3>危险操作</h3>
          <div>
            <button class="btn danger" @click="removeMiniApp">删除该小程序</button>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import { api, formatBytes } from '../api'

const items = ref([])
const error = ref('')

const showCreate = ref(false)
const creating = ref(false)
const createError = ref('')
const createForm = ref(emptyCreateForm())

const detail = ref(null)
const drawerError = ref('')
const infoForm = ref({})
const savingInfo = ref(false)
const iconInput = ref(null)
const packageFileInput = ref(null)
const packageForm = ref({ version: '', file: null })
const uploadingPackage = ref(false)

function emptyCreateForm() {
  return {
    id: '',
    title: '',
    summary: '',
    description: '',
    category: '',
    developer: '',
    version: '',
    published: true
  }
}

function openCreate() {
  createForm.value = emptyCreateForm()
  createError.value = ''
  showCreate.value = true
}

async function reload() {
  error.value = ''
  try {
    const data = await api.get('/api/v1/admin/miniapps')
    items.value = data.items || []
  } catch (err) {
    error.value = err.message || '加载失败'
  }
}

function applyItem(data) {
  const idx = items.value.findIndex((m) => m.id === data.id)
  if (idx >= 0) {
    items.value.splice(idx, 1, data)
  }
  if (detail.value && detail.value.id === data.id) {
    detail.value = data
  }
}

async function toggleFlag(m, value) {
  try {
    const updated = await api.patch(`/api/v1/admin/miniapps/${m.id}/flags`, { published: value })
    applyItem(updated)
  } catch (err) {
    error.value = err.message || '操作失败'
  }
}

async function createMiniApp() {
  createError.value = ''
  creating.value = true
  try {
    const form = createForm.value
    await api.post('/api/v1/admin/miniapps', {
      id: form.id.trim(),
      title: form.title.trim(),
      summary: form.summary.trim(),
      description: form.description,
      category: form.category.trim(),
      developer: form.developer.trim(),
      version: form.version.trim(),
      published: form.published
    })
    showCreate.value = false
    await reload()
  } catch (err) {
    createError.value = err.message || '创建失败'
  } finally {
    creating.value = false
  }
}

function openDetail(m) {
  drawerError.value = ''
  packageForm.value = { version: '', file: null }
  detail.value = m
  infoForm.value = {
    title: m.title,
    summary: m.summary,
    description: m.description,
    category: m.category,
    developer: m.developer,
    version: m.version
  }
}

function closeDetail() {
  detail.value = null
}

async function saveInfo() {
  savingInfo.value = true
  drawerError.value = ''
  try {
    const form = infoForm.value
    const updated = await api.put(`/api/v1/admin/miniapps/${detail.value.id}`, {
      title: form.title,
      summary: form.summary,
      description: form.description,
      category: form.category,
      developer: form.developer,
      version: form.version
    })
    applyItem(updated)
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
    const updated = await api.postForm(`/api/v1/admin/miniapps/${detail.value.id}/icon`, form)
    applyItem(updated)
  } catch (err) {
    drawerError.value = err.message || '上传失败'
  }
}

function onPackageFile(event) {
  packageForm.value.file = (event.target.files && event.target.files[0]) || null
}

async function uploadPackage() {
  const form = packageForm.value
  if (!form.file) return
  uploadingPackage.value = true
  drawerError.value = ''
  const data = new FormData()
  data.append('file', form.file)
  if (form.version.trim()) {
    data.append('version', form.version.trim())
  }
  try {
    const updated = await api.postForm(`/api/v1/admin/miniapps/${detail.value.id}/package`, data)
    applyItem(updated)
    packageForm.value = { version: '', file: null }
  } catch (err) {
    drawerError.value = err.message || '上传失败'
  } finally {
    uploadingPackage.value = false
  }
}

async function removeMiniApp() {
  if (!window.confirm(`确定删除小程序 ${detail.value.id} 吗？该操作不可恢复。`)) return
  try {
    await api.del(`/api/v1/admin/miniapps/${detail.value.id}`)
    closeDetail()
    await reload()
  } catch (err) {
    drawerError.value = err.message || '删除失败'
  }
}

onMounted(reload)
</script>
