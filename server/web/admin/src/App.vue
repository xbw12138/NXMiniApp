<template>
  <LoginView v-if="!authed" @success="onLogin" />
  <div v-else class="app">
    <header class="topbar">
      <div class="brand">
        <span class="logo">NX</span>
        <div>
          <h1>NX 商店管理后台</h1>
          <p>Switch 应用与小程序发布中心</p>
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
      <AppsTab v-else-if="tab === 'apps'" />
      <MiniAppsTab v-else-if="tab === 'miniapps'" />
      <DocsTab v-else />
    </main>
  </div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { getToken, setToken } from './api'
import LoginView from './components/LoginView.vue'
import OverviewTab from './components/OverviewTab.vue'
import AppsTab from './components/AppsTab.vue'
import MiniAppsTab from './components/MiniAppsTab.vue'
import DocsTab from './components/DocsTab.vue'

const tabs = [
  { key: 'overview', label: '概览' },
  { key: 'apps', label: '商店应用' },
  { key: 'miniapps', label: '小程序' },
  { key: 'docs', label: '文档' }
]

const tab = ref('overview')
const authed = ref(!!getToken())

function onLogin() {
  authed.value = true
}

function logout() {
  setToken('')
  authed.value = false
}

function onUnauthorized() {
  authed.value = false
}

onMounted(() => window.addEventListener('nxkit-unauthorized', onUnauthorized))
onBeforeUnmount(() => window.removeEventListener('nxkit-unauthorized', onUnauthorized))
</script>
