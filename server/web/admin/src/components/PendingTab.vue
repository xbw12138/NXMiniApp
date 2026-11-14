<template>
  <section>
    <div class="toolbar">
      <span class="muted">共 {{ items.length }} 个待审核投稿</span>
      <span class="spacer"></span>
      <button class="btn" :disabled="loading" @click="reload">{{ loading ? '刷新中…' : '刷新' }}</button>
    </div>
    <p v-if="error" class="error">{{ error }}</p>

    <div class="card">
      <table class="table">
        <thead>
          <tr>
            <th>图标</th>
            <th>标题</th>
            <th>ID</th>
            <th>开发者</th>
            <th>分类</th>
            <th>版本</th>
            <th>大小</th>
            <th>投稿时间</th>
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
            <td>{{ m.developer || '-' }}</td>
            <td>{{ m.category }}</td>
            <td class="mono">{{ m.version }}</td>
            <td>{{ formatBytes(m.size) }}</td>
            <td class="muted">{{ formatTime(m.created_at) }}</td>
            <td>
              <button class="btn small" @click="openDetail(m)">详情</button>
              <button class="btn small primary" :disabled="busy === m.id" @click="approve(m)">通过上架</button>
              <button class="btn small danger" :disabled="busy === m.id || m.protected" :title="m.protected ? '内置首页小程序受保护，不能删除' : ''" @click="remove(m)">删除</button>
            </td>
          </tr>
          <tr v-if="!items.length && !loading">
            <td colspan="9" class="empty">没有待审核的投稿</td>
          </tr>
        </tbody>
      </table>
    </div>

    <div v-if="detail" class="modal-mask" @click.self="detail = null">
      <div class="modal card">
        <h2>{{ detail.title }}</h2>
        <p class="muted mono">{{ detail.id }}</p>
        <p><span class="muted">开发者：</span>{{ detail.developer || '-' }}</p>
        <p><span class="muted">分类：</span>{{ detail.category }}</p>
        <p><span class="muted">版本：</span>{{ detail.version }}</p>
        <p><span class="muted">包大小：</span>{{ formatBytes(detail.size) }}</p>
        <p><span class="muted">投稿时间：</span>{{ formatTime(detail.created_at) }}</p>
        <p><span class="muted">简介：</span>{{ detail.summary || '-' }}</p>
        <label>详细描述<textarea :value="detail.description" rows="6" readonly></textarea></label>
        <button class="btn" @click="openPackage(detail)">下载安装包校验</button>
        <p v-if="detailError" class="error">{{ detailError }}</p>
        <div class="actions">
          <button class="btn ghost" @click="detail = null">关闭</button>
          <button class="btn danger" :disabled="busy === detail.id || detail.protected" :title="detail.protected ? '内置首页小程序受保护，不能删除' : ''" @click="remove(detail)">删除</button>
          <button class="btn primary" :disabled="busy === detail.id" @click="approve(detail)">通过上架</button>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import { api, formatBytes, formatTime } from '../api'

const emit = defineEmits(['changed'])

const items = ref([])
const loading = ref(false)
const error = ref('')
const busy = ref('')
const detail = ref(null)
const detailError = ref('')

async function reload() {
  loading.value = true
  error.value = ''
  try {
    const data = await api.get('/api/v1/admin/miniapps?status=pending')
    items.value = data.items || []
  } catch (err) {
    error.value = err.message || '加载失败'
  } finally {
    loading.value = false
  }
}

function openDetail(m) {
  detailError.value = ''
  detail.value = m
}

function openPackage(m) {
  window.open(m.download_url, '_blank', 'noopener')
}

function dropItem(id) {
  items.value = items.value.filter((m) => m.id !== id)
  if (detail.value && detail.value.id === id) {
    detail.value = null
  }
  emit('changed')
}

async function approve(m) {
  if (!window.confirm(`确认通过并上架「${m.title}」？上架后商店立即可见。`)) return
  busy.value = m.id
  error.value = ''
  detailError.value = ''
  try {
    await api.patch(`/api/v1/admin/miniapps/${m.id}/flags`, { published: true })
    dropItem(m.id)
  } catch (err) {
    const msg = err.message || '操作失败'
    error.value = msg
    detailError.value = msg
  } finally {
    busy.value = ''
  }
}

async function remove(m) {
  if (m.protected) {
    error.value = '内置首页小程序受保护，不能删除'
    return
  }
  if (!window.confirm(`确定删除投稿「${m.title}」吗？该操作不可恢复。`)) return
  busy.value = m.id
  error.value = ''
  detailError.value = ''
  try {
    await api.del(`/api/v1/admin/miniapps/${m.id}`)
    dropItem(m.id)
  } catch (err) {
    const msg = err.message || '删除失败'
    error.value = msg
    detailError.value = msg
  } finally {
    busy.value = ''
  }
}

onMounted(reload)
</script>
