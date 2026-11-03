<template>
  <div class="login-page">
    <form class="login-card" @submit.prevent="submit">
      <div class="logo big">NX</div>
      <h1>NX 商店管理后台</h1>
      <p class="muted">请输入管理令牌以继续</p>
      <input
        v-model="token"
        type="password"
        placeholder="管理令牌"
        autocomplete="current-password"
      />
      <p v-if="error" class="error">{{ error }}</p>
      <button class="btn primary block" :disabled="loading || !token.trim()">
        {{ loading ? '验证中…' : '登录' }}
      </button>
    </form>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { api, setToken } from '../api'

const emit = defineEmits(['success'])
const token = ref('')
const error = ref('')
const loading = ref(false)

async function submit() {
  error.value = ''
  loading.value = true
  try {
    const data = await api.post('/api/v1/admin/login', { token: token.value })
    setToken((data && data.token) || token.value)
    emit('success')
  } catch (err) {
    error.value = err.message || '登录失败'
  } finally {
    loading.value = false
  }
}
</script>
