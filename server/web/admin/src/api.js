// 后台默认使用 HttpOnly Cookie 会话（登录接口下发），前端不保存管理令牌。
// 脚本/CI 仍可直接用 Authorization: Bearer <token> 调用管理接口。
export class ApiError extends Error {
  constructor(message, status) {
    super(message)
    this.status = status
  }
}

async function request(path, { method = 'GET', json, form } = {}) {
  const headers = {}
  let body
  if (json !== undefined) {
    headers['Content-Type'] = 'application/json'
    body = JSON.stringify(json)
  } else if (form !== undefined) {
    body = form
  }
  const resp = await fetch(path, { method, headers, body, credentials: 'same-origin' })
  const isLogin = path.startsWith('/api/v1/admin/login')
  if (resp.status === 401 && !isLogin) {
    window.dispatchEvent(new CustomEvent('nxkit-unauthorized'))
  }
  let data = null
  const text = await resp.text()
  if (text) {
    try {
      data = JSON.parse(text)
    } catch (err) {
      data = null
    }
  }
  if (!resp.ok) {
    const detail = (data && data.detail) || `请求失败（${resp.status}）`
    throw new ApiError(detail, resp.status)
  }
  return data
}

export const api = {
  get: (path) => request(path),
  post: (path, json) => request(path, { method: 'POST', json }),
  postForm: (path, form) => request(path, { method: 'POST', form }),
  put: (path, json) => request(path, { method: 'PUT', json }),
  patch: (path, json) => request(path, { method: 'PATCH', json }),
  del: (path) => request(path, { method: 'DELETE' })
}

export function formatBytes(value) {
  const n = Number(value) || 0
  if (n < 1024) return `${n} B`
  const units = ['KB', 'MB', 'GB', 'TB']
  let size = n
  let i = -1
  do {
    size /= 1024
    i++
  } while (size >= 1024 && i < units.length - 1)
  return `${size.toFixed(1)} ${units[i]}`
}

export function formatTime(ms) {
  if (!ms) return '-'
  const d = new Date(Number(ms))
  const pad = (v) => String(v).padStart(2, '0')
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`
}
