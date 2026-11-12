<template>
  <LoginView v-if="!authed" @success="onLogin" />
  <div v-else class="app">
    <header class="topbar">
      <div class="brand">
        <span class="logo">NX</span>
        <div>
          <h1>NX 小程序管理后台</h1>
          <p>Switch 小程序发布中心</p>
        </div>
      </div>
      <button class="btn ghost" @click="logout">退出登录</button>
    </header>
    <nav class="tabs">
      <button
        v-for="t in tabs"
        :key="t.key"
        :class="['tab', { active: tab === t.key }]"
        @click="tab = t.key"
      >{{ t.label }}<span v-if="t.key === 'miniapps' && pendingCount > 0" class="tab-badge">{{ pendingCount > 99 ? '99+' : pendingCount }}</span></button>
    </nav>
    <main>
      <OverviewTab v-if="tab === 'overview'" />
      <AppsTab v-else-if="tab === 'store'" />
      <MiniAppsTab v-else-if="tab === 'miniapps'" />
      <HomeTab v-else-if="tab === 'home'" />
      <ReleasesTab v-else-if="tab === 'releases'" />
      <LogsTab v-else-if="tab === 'logs'" />
      <DocsTab v-else />
    </main>
  </div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { api, getToken, setToken } from './api'
import LoginView from './components/LoginView.vue'
import OverviewTab from './components/OverviewTab.vue'
import AppsTab from './components/AppsTab.vue'
import MiniAppsTab from './components/MiniAppsTab.vue'
import HomeTab from './components/HomeTab.vue'
import ReleasesTab from './components/ReleasesTab.vue'
import LogsTab from './components/LogsTab.vue'
import DocsTab from './components/DocsTab.vue'

const tabs = [
  { key: 'overview', label: '概览' },
  { key: 'store', label: '应用商店' },
  { key: 'miniapps', label: '小程序' },
  { key: 'home', label: '首页内容' },
  { key: 'releases', label: '容器版本' },
  { key: 'logs', label: '设备日志' },
  { key: 'docs', label: '文档' }
]

const tab = ref('overview')
const authed = ref(false)
const pendingCount = ref(0)

const DEFAULT_TITLE = 'NX 商店管理后台'
let notifyTimer = null

async function refreshNotifications() {
  try {
    const data = await api.get('/api/v1/admin/notifications')
    const count = Number(data && data.pending_miniapps) || 0
    pendingCount.value = count
    document.title = count > 0 ? `(${count}) ${DEFAULT_TITLE}` : DEFAULT_TITLE
  } catch (err) {
    // 轮询失败不打断后台使用，下一轮再试
  }
}

function startNotifications() {
  stopNotifications()
  refreshNotifications()
  notifyTimer = setInterval(refreshNotifications, 60 * 1000)
}

function stopNotifications() {
  if (notifyTimer) {
    clearInterval(notifyTimer)
    notifyTimer = null
  }
  pendingCount.value = 0
  document.title = DEFAULT_TITLE
}

function onLogin() {
  authed.value = true
  startNotifications()
}

async function logout() {
  try {
    await api.post('/api/v1/admin/logout')
  } catch (err) {
    // 会话可能已过期，忽略
  }
  setToken('')
  stopNotifications()
  authed.value = false
}

function onUnauthorized() {
  authed.value = false
  stopNotifications()
}

onMounted(async () => {
  window.addEventListener('nxkit-unauthorized', onUnauthorized)
  // 优先用 Cookie 会话（无需再次输入）；旧 token 仍兼容。
  if (getToken()) {
    authed.value = true
    startNotifications()
    return
  }
  try {
    await api.get('/api/v1/admin/session')
    authed.value = true
    startNotifications()
  } catch (err) {
    authed.value = false
  }
})
onBeforeUnmount(() => {
  window.removeEventListener('nxkit-unauthorized', onUnauthorized)
  stopNotifications()
})
</script>
