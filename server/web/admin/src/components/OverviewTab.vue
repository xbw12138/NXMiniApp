<template>
  <section>
    <p v-if="error" class="error">{{ error }}</p>
    <template v-if="stats">
      <div class="cards">
        <div class="card stat">
          <span class="stat-value">{{ stats.miniapps }}</span>
          <span class="stat-label">小程序总数</span>
        </div>
        <div class="card stat">
          <span class="stat-value">{{ stats.published }}</span>
          <span class="stat-label">已发布</span>
        </div>
        <div class="card stat">
          <span class="stat-value">{{ stats.releases }}</span>
          <span class="stat-label">容器版本</span>
        </div>
        <div class="card stat">
          <span class="stat-value">{{ stats.downloads }}</span>
          <span class="stat-label">累计下载</span>
        </div>
        <div class="card stat">
          <span class="stat-value">{{ stats.opens }}</span>
          <span class="stat-label">启动次数(PV)</span>
        </div>
        <div class="card stat">
          <span class="stat-value">{{ stats.uv }}</span>
          <span class="stat-label">独立设备(UV)</span>
        </div>
        <div class="card stat">
          <span class="stat-value">{{ formatBytes(stats.storage_bytes) }}</span>
          <span class="stat-label">存储占用</span>
        </div>
      </div>
      <div class="card">
        <div class="card-header"><h2>最近更新</h2></div>
        <table class="table">
          <thead>
            <tr>
              <th>小程序</th>
              <th>ID</th>
              <th>分类</th>
              <th>版本</th>
              <th>下载</th>
              <th>启动</th>
              <th>UV</th>
              <th>更新时间</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="app in stats.recent" :key="app.id">
              <td>
                <div class="appcell">
                  <img v-if="app.icon_url" :src="app.icon_url" class="icon" alt="" />
                  <div v-else class="icon placeholder">{{ (app.title || '?').slice(0, 1) }}</div>
                  <span>{{ app.title }}</span>
                </div>
              </td>
              <td class="mono">{{ app.id }}</td>
              <td>{{ app.category }}</td>
              <td>{{ app.version }}</td>
              <td>{{ app.downloads }}</td>
              <td>{{ app.opens }}</td>
              <td>{{ app.uv }}</td>
              <td class="muted">{{ formatTime(app.updated_at) }}</td>
            </tr>
            <tr v-if="!stats.recent.length">
              <td colspan="8" class="empty">暂无小程序</td>
            </tr>
          </tbody>
        </table>
      </div>
    </template>
  </section>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import { api, formatBytes, formatTime } from '../api'

const stats = ref(null)
const error = ref('')

onMounted(async () => {
  try {
    stats.value = await api.get('/api/v1/admin/stats')
  } catch (err) {
    error.value = err.message || '加载失败'
  }
})
</script>
