<template>
  <section>
    <div class="toolbar">
      <span class="muted">共 {{ items.length }} 份设备日志</span>
      <input
        v-model="keyword"
        class="filter-input"
        placeholder="按设备或文件名筛选"
      />
      <span class="spacer"></span>
      <button class="btn" @click="reload">刷新</button>
    </div>
    <p v-if="error" class="error">{{ error }}</p>

    <div class="card">
      <table class="table">
        <thead>
          <tr>
            <th>设备</th>
            <th>文件</th>
            <th>大小</th>
            <th>上传时间</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="item in filteredItems" :key="item.id">
            <td class="mono">{{ item.device }}</td>
            <td class="mono">{{ item.name }}</td>
            <td>{{ formatBytes(item.size) }}</td>
            <td>{{ formatTime(item.updated_at) }}</td>
            <td class="actions">
              <button class="btn small" @click="view(item)">查看</button>
              <a class="btn small" :href="downloadUrl(item)">下载</a>
              <button class="btn small danger" @click="remove(item)">删除</button>
            </td>
          </tr>
          <tr v-if="!filteredItems.length">
            <td colspan="5" class="empty">暂无匹配日志，先在 Switch 上用 Updater 上传一次</td>
          </tr>
        </tbody>
      </table>
    </div>

    <div v-if="viewing" class="modal-mask" @click.self="viewing = null">
      <div class="modal card log-modal">
        <h2>{{ viewing.device }} · {{ viewing.name }}</h2>
        <p v-if="truncated" class="muted">日志过长，仅显示末尾 512KB</p>
        <pre class="log-body">{{ content }}</pre>
        <div class="actions">
          <button class="btn ghost" @click="viewing = null">关闭</button>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { api, formatBytes, formatTime } from '../api'

const items = ref([])
const error = ref('')
const viewing = ref(null)
const content = ref('')
const truncated = ref(false)
const keyword = ref('')

const filteredItems = computed(() => {
  const needle = keyword.value.trim().toLowerCase()
  if (!needle) return items.value
  return items.value.filter(
    (item) =>
      item.device.toLowerCase().includes(needle) || item.name.toLowerCase().includes(needle)
  )
})

function downloadUrl(item) {
  return `/api/v1/admin/device-logs/content?id=${encodeURIComponent(item.id)}`
}

async function reload() {
  error.value = ''
  try {
    const data = await api.get('/api/v1/admin/device-logs')
    items.value = data.items || []
  } catch (err) {
    error.value = err.message || '加载失败'
  }
}

async function view(item) {
  viewing.value = item
  content.value = '加载中…'
  truncated.value = false
  try {
    const data = await api.get(`/api/v1/admin/device-logs/content?id=${encodeURIComponent(item.id)}`)
    content.value = data.content || ''
    truncated.value = !!data.truncated
  } catch (err) {
    content.value = err.message || '读取失败'
  }
}

async function remove(item) {
  if (!window.confirm(`删除 ${item.device} / ${item.name}？`)) return
  try {
    await api.del(`/api/v1/admin/device-logs?id=${encodeURIComponent(item.id)}`)
    await reload()
  } catch (err) {
    error.value = err.message || '删除失败'
  }
}

onMounted(reload)
</script>

<style scoped>
.filter-input {
  width: 240px;
  margin-left: 12px;
}
.log-modal {
  width: min(900px, 92vw);
}
.log-body {
  max-height: 60vh;
  overflow: auto;
  margin: 12px 0 0;
  padding: 12px;
  border-radius: 8px;
  background: #0b0d12;
  color: #c9d1d9;
  font-size: 12px;
  line-height: 1.5;
  white-space: pre-wrap;
  word-break: break-all;
}
</style>
