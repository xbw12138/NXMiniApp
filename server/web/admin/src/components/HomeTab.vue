<template>
  <section>
    <div class="toolbar">
      <span class="muted">首页由小程序驱动：容器启动时拉取并直接运行下方指定的首页小程序</span>
      <span class="spacer"></span>
      <button class="btn primary" @click="openCreate('banner')">新建横幅</button>
      <button class="btn" @click="openCreate('news')">新建资讯</button>
    </div>
    <p v-if="error" class="error">{{ error }}</p>

    <div class="card section">
      <h3>首页小程序</h3>
      <p class="muted">
        容器启动后直接运行该小程序，它负责渲染首页（横幅、资讯、推荐、最近使用）。留空则回落为内置列表页。
        改完即生效，无需重新编译 NRO。
      </p>
      <div class="row">
        <label style="flex: 1">
          首页小程序 ID
          <input v-model="homeForm.miniapp_id" placeholder="例如 home" list="miniapp-ids" />
          <datalist id="miniapp-ids">
            <option v-for="m in miniApps" :key="m.id" :value="m.id">{{ m.title }}</option>
          </datalist>
        </label>
        <button class="btn primary" :disabled="savingHome" @click="saveHome">
          {{ savingHome ? '保存中…' : '保存' }}
        </button>
      </div>
      <p v-if="homeError" class="error">{{ homeError }}</p>
    </div>

    <div class="card">
      <table class="table">
        <thead>
          <tr>
            <th>封面</th>
            <th>类型</th>
            <th>标题</th>
            <th>摘要</th>
            <th>跳转</th>
            <th>排序</th>
            <th>发布</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="item in items" :key="item.id">
            <td>
              <img v-if="item.cover_url" :src="item.cover_url" class="cover" alt="" />
              <div v-else class="cover placeholder">无</div>
            </td>
            <td>{{ item.kind === 'banner' ? '横幅' : '资讯' }}</td>
            <td>{{ item.title }}</td>
            <td class="muted ellipsis">{{ item.summary }}</td>
            <td class="mono">{{ linkLabel(item) }}</td>
            <td class="mono">{{ item.sort }}</td>
            <td>
              <label class="switch">
                <input
                  type="checkbox"
                  :checked="item.published"
                  @change="togglePublished(item, $event.target.checked)"
                />
                <span></span>
              </label>
            </td>
            <td>
              <button class="btn small" @click="openEdit(item)">编辑</button>
              <button class="btn small danger" @click="remove(item)">删除</button>
            </td>
          </tr>
          <tr v-if="!items.length">
            <td colspan="8" class="empty">暂无内容，先新建一条横幅或资讯</td>
          </tr>
        </tbody>
      </table>
    </div>

    <div v-if="editor" class="modal-mask" @click.self="editor = null">
      <form class="modal card" @submit.prevent="save">
        <h2>{{ editor.id ? '编辑内容' : (editor.kind === 'banner' ? '新建横幅' : '新建资讯') }}</h2>
        <label>标题<input v-model="editor.title" required /></label>
        <label>摘要（一行说明）<input v-model="editor.summary" /></label>
        <label>正文（资讯详情，纯文本分段）
          <textarea v-model="editor.body" rows="6"></textarea>
        </label>
        <div class="row">
          <label style="flex: 1">
            点击行为
            <select v-model="editor.link_type">
              <option value="none">无动作</option>
              <option value="miniapp">启动小程序</option>
              <option value="news">打开资讯详情</option>
              <option value="url">外部链接</option>
            </select>
          </label>
          <label style="flex: 1">
            跳转目标
            <input v-model="editor.link_target" placeholder="小程序 ID / 内容 ID / URL" />
          </label>
        </div>
        <div class="row">
          <label style="flex: 1">排序（小的靠前）<input v-model.number="editor.sort" type="number" /></label>
          <label class="check" style="align-self: flex-end">
            <input type="checkbox" v-model="editor.published" /> 发布
          </label>
        </div>
        <div class="card section">
          <h3>封面图</h3>
          <p class="muted">横幅建议 660×280（约 2.36:1），资讯封面建议 320×200。</p>
          <div class="row">
            <img v-if="editor.cover_url" :src="editor.cover_url" class="cover large" alt="" />
            <div v-else class="cover large placeholder">无封面</div>
            <input ref="coverInput" type="file" accept=".png,.jpg,.jpeg,.webp" hidden @change="uploadCover" />
            <button type="button" class="btn" :disabled="!editor.id" @click="coverInput && coverInput.click()">
              {{ editor.id ? '上传封面' : '先保存再上传封面' }}
            </button>
          </div>
        </div>
        <p v-if="editorError" class="error">{{ editorError }}</p>
        <div class="actions">
          <button type="button" class="btn ghost" @click="editor = null">取消</button>
          <button class="btn primary" :disabled="saving">{{ saving ? '保存中…' : '保存' }}</button>
        </div>
      </form>
    </div>
  </section>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import { api } from '../api'

const items = ref([])
const miniApps = ref([])
const error = ref('')

const homeForm = ref({ miniapp_id: '' })
const savingHome = ref(false)
const homeError = ref('')

const editor = ref(null)
const saving = ref(false)
const editorError = ref('')
const coverInput = ref(null)

async function reload() {
  error.value = ''
  try {
    const data = await api.get('/api/v1/admin/content')
    items.value = data.items || []
  } catch (err) {
    error.value = err.message || '加载失败'
  }
}

async function loadHome() {
  try {
    const data = await api.get('/api/v1/admin/home-config')
    homeForm.value.miniapp_id = data.miniapp_id || ''
  } catch (err) {
    homeError.value = err.message || '加载失败'
  }
}

async function loadMiniApps() {
  try {
    const data = await api.get('/api/v1/admin/miniapps')
    miniApps.value = data.items || []
  } catch (err) {
    /* 下拉建议失败不影响主流程 */
  }
}

async function saveHome() {
  homeError.value = ''
  savingHome.value = true
  try {
    const data = await api.put('/api/v1/admin/home-config', {
      miniapp_id: homeForm.value.miniapp_id.trim()
    })
    homeForm.value.miniapp_id = data.miniapp_id || ''
  } catch (err) {
    homeError.value = err.message || '保存失败'
  } finally {
    savingHome.value = false
  }
}

function openCreate(kind) {
  editorError.value = ''
  editor.value = {
    id: 0,
    kind,
    title: '',
    summary: '',
    body: '',
    cover_url: '',
    link_type: 'none',
    link_target: '',
    sort: 0,
    published: true
  }
}

function openEdit(item) {
  editorError.value = ''
  editor.value = { ...item }
}

async function save() {
  if (!editor.value) return
  editorError.value = ''
  saving.value = true
  const payload = {
    title: editor.value.title.trim(),
    summary: editor.value.summary.trim(),
    body: editor.value.body,
    link_type: editor.value.link_type,
    link_target: editor.value.link_target.trim(),
    sort: Number(editor.value.sort) || 0,
    published: editor.value.published
  }
  try {
    if (editor.value.id) {
      await api.put(`/api/v1/admin/content/${editor.value.id}`, payload)
    } else {
      await api.post('/api/v1/admin/content', { ...payload, kind: editor.value.kind })
    }
    editor.value = null
    await reload()
  } catch (err) {
    editorError.value = err.message || '保存失败'
  } finally {
    saving.value = false
  }
}

async function togglePublished(item, value) {
  try {
    await api.put(`/api/v1/admin/content/${item.id}`, { published: value })
    item.published = value
  } catch (err) {
    error.value = err.message || '操作失败'
  }
}

async function remove(item) {
  if (!window.confirm(`确定删除「${item.title}」？`)) return
  try {
    await api.del(`/api/v1/admin/content/${item.id}`)
    await reload()
  } catch (err) {
    error.value = err.message || '删除失败'
  }
}

async function uploadCover(event) {
  const file = event.target.files && event.target.files[0]
  if (!file || !editor.value || !editor.value.id) return
  const form = new FormData()
  form.append('file', file)
  try {
    const updated = await api.postForm(`/api/v1/admin/content/${editor.value.id}/cover`, form)
    editor.value.cover_url = updated.cover_url
    await reload()
  } catch (err) {
    editorError.value = err.message || '上传失败'
  } finally {
    event.target.value = ''
  }
}

function linkLabel(item) {
  if (item.link_type === 'miniapp') return `小程序 ${item.link_target}`
  if (item.link_type === 'news') return '资讯详情'
  if (item.link_type === 'url') return item.link_target || '外部链接'
  return '-'
}

onMounted(async () => {
  await Promise.all([reload(), loadHome(), loadMiniApps()])
})
</script>

<style scoped>
.ellipsis {
  max-width: 220px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.cover {
  width: 88px;
  height: 40px;
  object-fit: cover;
  border-radius: 6px;
  background: #1b1f27;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 12px;
  color: #6b7280;
}
.cover.large {
  width: 264px;
  height: 112px;
  font-size: 14px;
}
.cover.placeholder {
  border: 1px dashed #2c323c;
}
</style>
