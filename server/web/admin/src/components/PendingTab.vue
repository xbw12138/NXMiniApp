<template>
  <section>
    <div class="toolbar">
      <span class="muted">共 {{ items.length }} 条待审核</span>
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
            <th>审核类型</th>
            <th>版本</th>
            <th>大小</th>
            <th>提交时间</th>
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
            <td><span class="tag">{{ reviewType(m) }}</span></td>
            <td class="mono">{{ reviewVersion(m) }}</td>
            <td>{{ formatBytes(reviewSize(m)) }}</td>
            <td class="muted">{{ formatTime(reviewTime(m)) }}</td>
            <td>
              <button class="btn small" @click="openDetail(m)">详情</button>
              <button class="btn small primary" :disabled="busy === m.id" @click="approve(m)">通过</button>
              <button class="btn small danger" :disabled="busy === m.id" @click="reject(m)">驳回</button>
              <button class="btn small danger" :disabled="busy === m.id || m.protected" :title="m.protected ? '内置首页小程序受保护，不能删除' : ''" @click="remove(m)">删除</button>
            </td>
          </tr>
          <tr v-if="!items.length && !loading">
            <td colspan="10" class="empty">没有待审核的投稿</td>
          </tr>
        </tbody>
      </table>
    </div>

    <div v-if="detail" class="modal-mask" @click.self="detail = null">
      <div class="modal card">
        <h2>{{ detail.title }}</h2>
        <p class="muted mono">{{ detail.id }}</p>
        <p><span class="muted">审核类型：</span>{{ reviewType(detail) }}</p>
        <p><span class="muted">提交版本：</span>{{ reviewVersion(detail) }}</p>
        <p><span class="muted">包大小：</span>{{ formatBytes(reviewSize(detail)) }}</p>
        <p><span class="muted">提交时间：</span>{{ formatTime(reviewTime(detail)) }}</p>
        <p><span class="muted">当前状态：</span>{{ detail.review_status }}{{ detail.published ? '（已上架）' : '（未上架）' }}</p>
        <p><span class="muted">开发者：</span>{{ detail.developer || '-' }}</p>
        <p><span class="muted">分类：</span>{{ detail.category }}</p>
        <p><span class="muted">简介：</span>{{ detail.summary || '-' }}</p>
        <label>详细描述<textarea :value="detail.description" rows="5" readonly></textarea></label>
        <div v-if="detail.review_note" class="review-note">
          <span class="muted">上次审核意见：</span>{{ detail.review_note }}
        </div>
        <label>驳回理由<textarea v-model="rejectNote" rows="2" placeholder="驳回时必填，会展示给上传者"></textarea></label>
        <button class="btn" @click="openPackage(detail)">下载安装包校验</button>
        <button class="btn" :disabled="busy === detail.id" @click="resetKey(detail)">重置开发者密钥</button>
        <p v-if="detailError" class="error">{{ detailError }}</p>
        <div class="actions">
          <button class="btn ghost" @click="detail = null">关闭</button>
          <button class="btn danger" :disabled="busy === detail.id || detail.protected" :title="detail.protected ? '内置首页小程序受保护，不能删除' : ''" @click="remove(detail)">删除</button>
          <button class="btn danger" :disabled="busy === detail.id" @click="reject(detail)">驳回</button>
          <button class="btn primary" :disabled="busy === detail.id" @click="approve(detail)">通过审核</button>
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
const rejectNote = ref('')

function pending(m) {
  return m.pending || null
}

function reviewType(m) {
  const p = pending(m)
  if (!p) return '首次投稿'
  if (p.kind === 'gray') return '灰度版本'
  if (m.review_status === 'approved') return '版本更新'
  if (m.review_status === 'rejected') return '重新投稿'
  return '首次投稿'
}

function reviewVersion(m) {
  const p = pending(m)
  return (p && p.version) || m.version || '-'
}

function reviewSize(m) {
  const p = pending(m)
  return (p && p.size) || m.size || 0
}

function reviewTime(m) {
  const p = pending(m)
  return (p && p.submitted_at) || m.created_at || 0
}

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
  rejectNote.value = ''
  detail.value = m
}

function openPackage(m) {
  window.open(`/api/v1/admin/miniapps/${m.id}/pending/download`, '_blank', 'noopener')
}

function dropItem(id) {
  items.value = items.value.filter((m) => m.id !== id)
  if (detail.value && detail.value.id === id) {
    detail.value = null
  }
  emit('changed')
}

async function approve(m) {
  if (!window.confirm(`确认通过「${m.title}」的${reviewType(m)} ${reviewVersion(m)}？通过后立即${m.review_status === 'approved' && m.published ? '替换线上版本' : '上架'}。`)) return
  busy.value = m.id
  error.value = ''
  detailError.value = ''
  try {
    await api.post(`/api/v1/admin/miniapps/${m.id}/review`, { approve: true })
    dropItem(m.id)
  } catch (err) {
    const msg = err.message || '操作失败'
    error.value = msg
    detailError.value = msg
  } finally {
    busy.value = ''
  }
}

async function reject(m) {
  const note = detail.value && detail.value.id === m.id && rejectNote.value.trim()
    ? rejectNote.value.trim()
    : window.prompt(`驳回「${m.title}」的理由（会展示给上传者）：`, '')
  if (note === null) return
  if (!note.trim()) {
    const msg = '请填写驳回理由'
    error.value = msg
    detailError.value = msg
    return
  }
  busy.value = m.id
  error.value = ''
  detailError.value = ''
  try {
    await api.post(`/api/v1/admin/miniapps/${m.id}/review`, { approve: false, note: note.trim() })
    dropItem(m.id)
  } catch (err) {
    const msg = err.message || '操作失败'
    error.value = msg
    detailError.value = msg
  } finally {
    busy.value = ''
  }
}

async function resetKey(m) {
  if (!window.confirm(`确定重置「${m.title}」的开发者密钥？旧密钥会立即失效。`)) return
  busy.value = m.id
  detailError.value = ''
  try {
    const data = await api.post(`/api/v1/admin/miniapps/${m.id}/reset-key`, {})
    window.prompt('新的开发者密钥（只显示一次，请复制后交给上传者）：', data.dev_key || '')
  } catch (err) {
    detailError.value = err.message || '重置失败'
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
