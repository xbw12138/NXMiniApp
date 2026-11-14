# NXMiniApp 小程序能力速查

完整属性与行为以仓库 `docs/` 为准（组件 `docs/components/index.html`，API `docs/api/index.html`，样式 `docs/style/index.html`，排错 `docs/reference/errors.html`）。本表用于快速判断「能不能做」，不确定时必须回文档确认。

## 内置标签（22 类，常用项）

| 标签 | 默认尺寸 | 可聚焦 | 常用事件 |
|---|---|---|---|
| `view` | 自适应 | 否 | `bindtap` |
| `scroll-view` | 自适应 | 子节点 | `bindtap` |
| `text` | 按文本测量 | 否 | `bindtap` |
| `rich-text` | 宽 100% | 否 | — |
| `image` | 200×120rpx | 否 | `bindtap` |
| `icon` | 48×48rpx | 否 | — |
| `button` | 320×88rpx | 是 | `bindtap` |
| `navigator` | 100%×80rpx | 是 | `bindtap` + 路由 |
| `input` | 100%×80rpx | 是 | `bindinput` / `bindconfirm` |
| `textarea` | 100%×160rpx | 是 | `bindinput` / `bindconfirm` |
| `switch` | 96×56rpx | 是 | `bindchange` |
| `checkbox` / `radio` | 48×48rpx | 是 | `bindchange` |
| `slider` | 100%×80rpx | 是 | `bindchange` |
| `progress` | 100%×24rpx | 否 | — |
| `swiper` | 100%×300rpx | 自动轮播 | — |
| `swiper-item` | 100%×100% | 否 | — |
| `video` | 100%×360rpx | 是 | `bindplay` / `bindpause` / `bindended` / `bindtimeupdate` / `binderror` |
| `canvas` | 100%×300rpx | 是 | 2D retained-mode，`wx.createCanvasContext(id)` + `ctx.draw()`；触摸/长按事件可用 |

未知标签静默降级为 `view`；没有自定义组件。

## wx.* API（40+ 个真实可用）

- **网络**：`wx.request`（GET/POST/PUT/DELETE，回调 `success({data,statusCode,header})`）、`wx.getNetworkType`、`wx.connectSocket`（ws/wss，onOpen/onMessage/onError/onClose、send/close）
- **界面**：`wx.showToast`、`wx.showLoading` / `wx.hideLoading`、`wx.showModal`（`success({confirm,cancel})`）、`wx.setNavigationBarTitle`、`wx.nextTick`、`wx.vibrateShort`（Switch HD 震动）、`wx.canIUse`
- **游戏**：`wx.getGamepadState()`（axes + buttons + pressed）、`requestAnimationFrame` / `cancelAnimationFrame`、`wx.createCanvasContext(id)`（fillRect/strokeRect/clearRect/路径/arc/fillText/drawImage/变换）
- **视频**：`wx.createVideoContext(id)` → `play()` / `pause()` / `stop()` / `seek(seconds)`
- **音频**：`wx.createInnerAudioContext()` → `src` / `autoplay` / `loop` / `volume` / `startTime`、`play()` / `pause()` / `stop()` / `seek()` / `destroy()`、`onPlay` / `onEnded` / `onError` 等
- **存储**（按小程序隔离）：`wx.setStorageSync` / `getStorageSync` / `removeStorageSync` / `clearStorageSync` 及异步回调版；`wx.getStorageInfoSync()` → `{keys,currentSize,limitSize}`
- **路由**：`wx.navigateTo`（url 可带 query）、`wx.navigateBack({delta})`、`wx.redirectTo`、`wx.reLaunch`
- **系统**：`wx.getSystemInfoSync` / `getSystemInfo`、`getWindowInfo`、`getDeviceInfo`、`getAppBaseInfo`、`getMenuButtonBoundingClientRect`（零矩形）、`getLaunchOptionsSync` / `getEnterOptionsSync`
- **其他**：`wx.setClipboardData` / `getClipboardData`、`wx.getUpdateManager`（空实现）
- **容器扩展**：`wx.miniapp.deviceId()`（容器持久化设备标识，用于服务端 PV/UV 统计）

另有 18 个兼容占位接口：调用不报错也没有行为，便于从微信迁移。用 `wx.canIUse('schema')` 探测。

## 不支持能力清单

容器是自研 WXML/WXSS 渲染器（Borealis + NanoVG + QuickJS），不是微信运行时。以下能力**不保证可用**，涉及时需要先改容器或换实现：

| 能力 | 现状 / 替代方案 |
|---|---|
| `web-view` | 不支持，未知标签会降级为 `view`；需要网页请用外部浏览器 |
| 自定义组件 / `usingComponents` | 不支持，只能使用内置 22 类标签 |
| npm / 构建工具 | 不支持，JS 直接运行，无打包步骤 |
| 微信登录、支付、分享、订阅消息 | 不支持；`wx.login` 等仅占位 |
| 云开发 / 云函数 | 不支持，请自带 HTTP 服务端 |
| 广告、激励视频 | 不支持 |
| `wx.createAnimation` / CSS 动画 | 不支持；用 `setData` 切换样式或逐帧绘制 |
| 文件系统 API（`wx.getFileSystemManager` 等） | 不支持；持久化用 `wx.*Storage*` |
| Worker / 多线程 | 不支持；耗时任务用 `setTimeout` 分片 |
| `wx.getUpdateManager` | 空实现；小程序更新由容器按 `version` 自动重下 |
| 小程序菜单按钮 / 胶囊 | `getMenuButtonBoundingClientRect` 返回零矩形，布局不要依赖它 |
| 位置、蓝牙、NFC、传感器 | 不支持（Switch 无对应能力） |
| 自定义导航栏、下拉刷新 | 不支持，导航由容器统一处理 |
| 桌面滚轮滚动 | 未接入；`scroll-view` 支持触屏拖动与焦点自动滚动 |
| 部分 WXSS 特性 | 以 `docs/style/index.html` 为准；未列出的选择器/属性可能被忽略 |

判断原则：**文档里写了才可用**，未写的能力默认按不支持处理，不要凭微信经验直接写。

## 页面生命周期

`onLoad(query)` → `onShow()` → 首次绘制后 `onReady()`；压栈时 `onHide()`，出栈时 `onUnload()`。

## 页面模板

```js
// pages/index/index.js
Page({
  data: { msg: 'Hello' },
  onLoad(query) {},
  change() { this.setData({ msg: 'NXMiniApp' }); }
});
```

```html
<!-- pages/index/index.wxml -->
<view class="page">
  <text>{{ msg }}</text>
  <button bindtap="change">点我</button>
</view>
```

## 打包

| 场景 | 要求 | 命令 |
|---|---|---|
| 投稿到服务端 | 普通 zip（服务端重打包为 stored） | `zip -r app.zip .` |
| 容器直接读取（.nxpkg / 本地预览） | **ZIP_STORED、无数据描述符** | `cd pkg && zip -X -0 -r ../app.nxpkg .` |

约束：包根直接含 `app.json`；不允许绝对路径与 `..`；`meta.json` 与 `icon.png` 只用于商店目录，不进安装包。

## 商店接口（公开）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/v1/health` | 健康检查 |
| GET | `/api/v1/miniapps` | 列表（仅已发布），支持 `q`、`category` |
| GET | `/api/v1/miniapps/{id}` | 详情 |
| GET | `/api/v1/miniapps/{id}/download` | 下载 `.nxpkg`（始终 ZIP_STORED） |
| GET | `/api/v1/miniapps/{id}/icon` | 封面（可能 404） |
| POST | `/api/v1/check-updates` | 批量版本检查 |
| POST | `/api/v1/submissions` | 免登录投稿（multipart） |
| POST | `/api/v1/miniapps/{id}/open` | 启动上报（PV/UV，body 可带 `device_id`） |
| GET | `/api/v1/miniapp-home` | 小程序首页描述（`home` + 横幅/资讯/推荐） |
| GET | `/api/v1/container/releases` | 容器 NRO 版本列表（仅已发布，首个为最新） |

## 上架字段（meta.json / 投稿表单）

| 字段 | 默认 | 说明 |
|---|---|---|
| `id` | 目录名 | `^[a-z0-9][a-z0-9_-]{1,63}$` |
| `title` | 目录名 | 卡片标题（投稿必填） |
| `summary` | 空 | 一句话简介，参与搜索 |
| `description` | 空 | 详情描述 |
| `category` | 示例 | 工具 / 娱乐 / 教育 / 生活 / 游戏 / 示例 / 其他 |
| `developer` | 空 | 开发者 |
| `version` | 1.0.0 | 与本地 `.version` 比较决定是否重新下载 |
| `published` | true | false 时不出现在商店列表 |

## 自动生成清单

<!-- BEGIN:generated -->
<!-- 本段由 scripts/gen-capabilities.sh 自动生成，请勿手改 -->

### 支持的 WXML 标签

`button`, `canvas`, `checkbox`, `checkbox-group`, `form`, `icon`, `image`, `input`, `label`, `navigator`, `progress`, `radio`, `radio-group`, `rich-text`, `scroll-view`, `slider`, `swiper`, `swiper-item`, `switch`, `text`, `textarea`, `video`, `view`

### 公开 API（/api/v1）

| 方法 | 路径 |
|---|---|
| GET | `/api/v1/health` |
| GET | `/api/v1/categories` |
| GET | `/api/v1/home` |
| GET | `/api/v1/apps` |
| GET | `/api/v1/apps/:id` |
| GET | `/api/v1/apps/:id/download` |
| POST | `/api/v1/check-updates` |
| GET | `/api/v1/miniapps` |
| GET | `/api/v1/miniapp-home` |
| GET | `/api/v1/tv/channels` |
| GET | `/api/v1/content/:id` |
| POST | `/api/v1/content/:id/open` |
| GET | `/api/v1/miniapps/:id` |
| GET | `/api/v1/miniapps/:id/download` |
| POST | `/api/v1/miniapps/:id/open` |
| GET | `/api/v1/miniapps/:id/icon` |
| POST | `/api/v1/submissions` |
| GET | `/api/v1/container/releases` |
| GET | `/api/v1/container/releases/:id/download` |
| POST | `/api/v1/device/logs` |
<!-- END:generated -->

