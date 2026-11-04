<template>
  <section class="docs">
    <p v-if="error" class="error">{{ error }}</p>
    <p v-if="notice" class="notice">{{ notice }}</p>
    <div class="docs-layout">
      <div class="card docs-list">
        <div class="card-header"><h2>页面</h2></div>
        <button
          v-for="p in pages"
          :key="p.path"
          :class="['doc-item', { active: current && current.path === p.path }]"
          @click="load(p.path)"
        >
          <span>{{ p.title }}</span>
          <span class="muted mono">{{ p.path }}</span>
        </button>
        <p v-if="!pages.length" class="empty">暂无文档</p>
      </div>

      <div v-if="current" class="card docs-editor">
        <div class="toolbar">
          <a class="btn ghost small" :href="current.url" target="_blank" rel="noopener">预览 ↗</a>
          <span class="muted">更新于 {{ formatTime(current.updated_at) }}</span>
          <span class="spacer"></span>
          <button class="btn danger small" :disabled="saving" @click="reset">恢复默认</button>
          <button class="btn primary small" :disabled="saving" @click="save">
            {{ saving ? '保存中…' : '保存' }}
          </button>
        </div>
        <div class="section">
          <label>
            页面标题（&lt;title&gt;）
            <input v-model="current.title" />
          </label>
          <label>
            顶栏描述（topbar-meta）
            <input v-model="current.meta" />
          </label>
          <label>
            主内容 HTML（&lt;main class="content"&gt; 内部）
            <textarea v-model="current.content" class="doc-content mono" rows="24" spellcheck="false"></textarea>
          </label>
        </div>
      </div>
      <div v-else class="card docs-editor">
        <p class="empty">从左侧选择一个页面开始编辑</p>
      </div>
    </div>
  </section>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import { api, formatTime } from '../api'

const pages = ref([])
const current = ref(null)
const error = ref('')
const notice = ref('')
const saving = ref(false)

async function refresh(keepPath) {
  const data = await api.get('/api/v1/admin/docs')
  pages.value = data.items || []
  if (keepPath) {
    const found = pages.value.find((p) => p.path === keepPath)
    if (found) await load(found.path)
  }
}

async function load(path) {
  error.value = ''
  notice.value = ''
  try {
    current.value = await api.get('/api/v1/admin/docs/page?path=' + encodeURIComponent(path))
  } catch (err) {
    error.value = err.message || '加载失败'
  }
}

async function save() {
  if (!current.value || saving.value) return
  saving.value = true
  error.value = ''
  notice.value = ''
  try {
    const saved = await api.put('/api/v1/admin/docs/page', {
      path: current.value.path,
      title: current.value.title,
      meta: current.value.meta,
      content: current.value.content
    })
    current.value = saved
    notice.value = '已保存'
    await refresh(saved.path)
  } catch (err) {
    error.value = err.message || '保存失败'
  } finally {
    saving.value = false
  }
}

async function reset() {
  if (!current.value || saving.value) return
  if (!window.confirm(`确定将「${current.value.title}」恢复为默认内容吗？当前修改将丢失。`)) return
  saving.value = true
  error.value = ''
  notice.value = ''
  try {
    const restored = await api.post('/api/v1/admin/docs/reset', { path: current.value.path })
    current.value = restored
    notice.value = '已恢复默认'
    await refresh(restored.path)
  } catch (err) {
    error.value = err.message || '恢复失败'
  } finally {
    saving.value = false
  }
}

onMounted(async () => {
  try {
    await refresh()
  } catch (err) {
    error.value = err.message || '加载失败'
  }
})
</script>

<style scoped>
.docs-layout {
  display: grid;
  grid-template-columns: 280px 1fr;
  gap: 16px;
  align-items: start;
}

.docs-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
  max-height: 78vh;
  overflow-y: auto;
}

.doc-item {
  display: flex;
  flex-direction: column;
  gap: 2px;
  text-align: left;
  background: none;
  border: 1px solid transparent;
  border-radius: 8px;
  padding: 8px 10px;
  color: var(--text);
  cursor: pointer;
  font-size: 14px;
}

.doc-item:hover {
  border-color: var(--border);
}

.doc-item.active {
  border-color: var(--accent);
  background: var(--bg-soft);
}

.doc-item .mono {
  font-size: 12px;
}

.docs-editor .toolbar {
  margin-bottom: 12px;
}

.doc-content {
  resize: vertical;
  min-height: 320px;
  line-height: 1.6;
  white-space: pre;
}

.notice {
  color: var(--accent);
  font-size: 13px;
  margin: 8px 0;
}

@media (max-width: 900px) {
  .docs-layout {
    grid-template-columns: 1fr;
  }
}
</style>
