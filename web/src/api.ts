export type ParametersDto = {
  rsi_period: number
  rsi_overbought: number
  rsi_oversold: number
  timeframe: string
  hammer_multiplier: number
  volume_surge_multiplier: number
  trading_side: 'LONG' | 'SHORT' | 'BOTH'
  take_profit_scope: 'CYCLE' | 'TRADE'
  rsi_indicators: RsiIndicator[]
  hammer_indicators: HammerIndicator[]
  volume_indicators: VolumeIndicator[]
}

export type RsiIndicator = { period: number; overbought: number; oversold: number; timeframe: string }
export type HammerIndicator = { timeframe: string; multiplier: number }
export type VolumeIndicator = { timeframe: string; multiplier: number }

export type BotConfig = {
  id: number
  symbol: string
  updatedAt: string
  active: boolean
  price_step_percentage: number
  take_profit_percentage: number
  trade_quantity: number
  last_long_price: number
  last_short_price: number
  parameters: ParametersDto
}

export type BotDraft = {
  active: boolean
  price_step_percentage: number
  take_profit_percentage: number
  trade_quantity: number
  parameters: ParametersDto
}

export class ApiError extends Error {
  status: number
  constructor(status: number, message: string) {
    super(message)
    this.status = status
  }
}

function csrfToken() {
  const match = document.cookie.match(/(?:^|; )XSRF-TOKEN=([^;]*)/)
  return match ? decodeURIComponent(match[1]) : undefined
}

let csrfBootstrap: Promise<unknown> | null = null

export function ensureCsrf(force = false) {
  if (!force && csrfToken()) return Promise.resolve()
  csrfBootstrap ??= request('/v1/auth/browser/csrf').finally(() => { csrfBootstrap = null })
  return csrfBootstrap
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  return requestOnce<T>(path, init, true)
}

async function requestOnce<T>(path: string, init: RequestInit, retryCsrf: boolean): Promise<T> {
  const method = init.method ?? 'GET'
  if (method !== 'GET' && method !== 'HEAD') await ensureCsrf()
  const headers = new Headers(init.headers)
  if (init.body && !headers.has('Content-Type')) headers.set('Content-Type', 'application/json')
  if (method !== 'GET' && method !== 'HEAD') {
    const token = csrfToken()
    if (token) headers.set('X-XSRF-TOKEN', token)
  }

  const response = await fetch(`/api${path}`, { ...init, headers, credentials: 'include' })
  if (!response.ok) {
    if (response.status === 403 && retryCsrf && method !== 'GET' && method !== 'HEAD' && !path.startsWith('/v1/auth/browser/')) {
      await ensureCsrf(true)
      return requestOnce<T>(path, init, false)
    }
    const message = await response.text()
    throw new ApiError(response.status, message || 'The request could not be completed')
  }
  if (response.status === 204) return undefined as T
  const contentType = response.headers.get('content-type') ?? ''
  return contentType.includes('application/json') ? response.json() : undefined as T
}

export function login(email: string, password: string) {
  return request<{ email: string }>('/v1/auth/browser/login', { method: 'POST', body: JSON.stringify({ email, password }) })
}

export function register(email: string, password: string) {
  return request<{ email: string }>('/v1/auth/browser/register', { method: 'POST', body: JSON.stringify({ email, password }) })
}

export function refresh() {
  return request<{ email: string }>('/v1/auth/browser/refresh', { method: 'POST', body: '{}' })
}

export function logout() {
  return request<void>('/v1/auth/browser/logout', { method: 'POST', body: '{}' })
}

export function getBots() {
  return request<BotConfig[]>('/v1/configs')
}

export function saveBot(symbol: string, draft: BotDraft) {
  const path = `/v1/config/${encodeURIComponent(symbol)}`
  const init = {
    method: 'PUT',
    body: JSON.stringify(draft),
  } satisfies RequestInit
  return request<BotConfig>(path, init).catch(async error => {
    if (!(error instanceof ApiError) || (error.status !== 401 && error.status !== 403)) throw error
    await refresh()
    return request<BotConfig>(path, init)
  })
}

export function deleteBot(symbol: string) {
  return request<void>(`/v1/config/${encodeURIComponent(symbol)}`, { method: 'DELETE' })
}

export function saveCredentials(apiKey: string, apiSecret: string) {
  return request<void>('/v1/me/exchange-credentials', {
    method: 'PUT',
    body: JSON.stringify({ apiKey, apiSecret }),
  })
}
