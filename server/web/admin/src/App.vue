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
      >{{ t.label }}</button>
    </nav>
    <main>
      <OverviewTab v-if="tab === 'overview'" />
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
import MiniAppsTab from './components/MiniAppsTab.vue'
import HomeTab from './components/HomeTab.vue'
import ReleasesTab from './components/ReleasesTab.vue'
import LogsTab from './components/LogsTab.vue'
import DocsTab from './components/DocsTab.vue'

const tabs = [
  { key: 'overview', label: '概览' },
  { key: 'miniapps', label: '小程序' },
  { key: 'home', label: '首页内容' },
  { key: 'releases', label: '容器版本' },
  { key: 'logs', label: '设备日志' },
  { key: 'docs', label: '文档' }
]

const tab = ref('overview')
const authed = ref(false)

function onLogin() {
  authed.value = true
}

async function logout() {
  try {
    await api.post('/api/v1/admin/logout')
  } catch (err) {
    // 会话可能已过期，忽略
  }
  setToken('')
  authed.value = false
}

function onUnauthorized() {
  authed.value = false
}

onMounted(async () => {
  window.addEventListener('nxkit-unauthorized', onUnauthorized)
  // 优先用 Cookie 会话（无需再次输入）；旧 token 仍兼容。
  if (getToken()) {
    authed.value = true
    return
  }
  try {
    await api.get('/api/v1/admin/session')
    authed.value = true
  } catch (err) {
    authed.value = false
  }
})
onBeforeUnmount(() => window.removeEventListener('nxkit-unauthorized', onUnauthorized))
</script>
