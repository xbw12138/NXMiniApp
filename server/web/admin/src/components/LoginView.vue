<template>
  <div class="login-page">
    <form class="login-card" @submit.prevent="submit">
      <div class="logo big">NX</div>
      <h1>NX 商店管理后台</h1>

      <template v-if="step === 'password'">
        <p class="muted">请输入管理员密码</p>
        <input
          v-model="password"
          type="password"
          placeholder="密码"
          autocomplete="current-password"
        />
      </template>

      <template v-else>
        <p class="muted">输入 2FAS / 验证器中的 6 位动态码</p>
        <input
          v-model="code"
          class="code"
          inputmode="numeric"
          maxlength="6"
          placeholder="000000"
          autocomplete="one-time-code"
        />
      </template>

      <p v-if="error" class="error">{{ error }}</p>
      <button class="btn primary block" :disabled="loading || !canSubmit">
        {{ loading ? '验证中…' : step === 'password' ? '下一步' : '登录' }}
      </button>
      <button v-if="step === 'totp'" type="button" class="btn ghost block" @click="reset">
        重新输入密码
      </button>
    </form>
  </div>
</template>

<script setup>
import { computed, ref } from 'vue'
import { api } from '../api'

const emit = defineEmits(['success'])
const step = ref('password')
const password = ref('')
const code = ref('')
const challenge = ref('')
const error = ref('')
const loading = ref(false)

const canSubmit = computed(() => {
  if (loading.value) return false
  return step.value === 'password' ? password.value.length > 0 : code.value.trim().length === 6
})

function reset() {
  step.value = 'password'
  password.value = ''
  code.value = ''
  challenge.value = ''
  error.value = ''
}

async function submit() {
  if (!canSubmit.value) return
  error.value = ''
  loading.value = true
  try {
    if (step.value === 'password') {
      const data = await api.post('/api/v1/admin/login/password', {
        password: password.value
      })
      challenge.value = data.challenge
      step.value = 'totp'
      code.value = ''
    } else {
      await api.post('/api/v1/admin/login/totp', {
        challenge: challenge.value,
        code: code.value.trim()
      })
      emit('success')
    }
  } catch (err) {
    error.value = err.message || '登录失败'
  } finally {
    loading.value = false
  }
}
</script>
