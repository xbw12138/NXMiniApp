# NXMiniApp 速查表

组件、事件、API 与分发接口的快速参考。完整属性与行为以仓库 `docs/` 为准（组件：`docs/components/index.html`，API：`docs/api/index.html`）。

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

未知标签降级为 `view`；无自定义组件。

## wx.* API（36 个真实可用，按分组）

- **网络**：`wx.request`（GET/POST/PUT/DELETE，回调 `success({data,statusCode,header})`）、`wx.getNetworkType`
- **界面**：`wx.showToast`、`wx.showLoading` / `wx.hideLoading`、`wx.showModal`（`success({confirm,cancel})`）、`wx.setNavigationBarTitle`、`wx.nextTick`、`wx.vibrateShort`（空实现）、`wx.canIUse`
- **视频**：`wx.createVideoContext(id)` → `play()` / `pause()` / `stop()` / `seek(seconds)`（全屏为空实现）
- **存储**（按小程序隔离）：`wx.setStorageSync` / `wx.getStorageSync` / `wx.removeStorageSync` / `wx.clearStorageSync` 及异步回调版；`wx.getStorageInfoSync()` → `{keys,currentSize,limitSize}`
- **路由**：`wx.navigateTo`（url 可带 query）、`wx.navigateBack({delta})`、`wx.redirectTo`、`wx.reLaunch`
- **系统**：`wx.getSystemInfoSync` / `wx.getSystemInfo`、`wx.getWindowInfo`、`wx.getDeviceInfo`、`wx.getAppBaseInfo`、`wx.getMenuButtonBoundingClientRect`（零矩形）、`wx.getLaunchOptionsSync` / `wx.getEnterOptionsSync`
- **其他**：`wx.setClipboardData` / `wx.getClipboardData`、`wx.getUpdateManager`（空实现）

另有 18 个兼容占位接口：调用不报错但无行为，便于从微信小程序迁移。用 `wx.canIUse` 探测。

## 页面生命周期

`onLoad(query)` → `onShow()` → 首次绘制后 `onReady()`；压栈时 `onHide()`，出栈时 `onUnload()`，共 6 个。

## 页面模板

```js
// pages/index/index.js
Page({
  data: { msg: 'Hello' },
  onLoad(query) {},
  onShow() {},
  change() {
    this.setData({ msg: 'NXMiniApp' });
  }
});
```

```html
<!-- pages/index/index.wxml -->
<view class="page">
  <text>{{ msg }}</text>
  <button bindtap="change">点我</button>
</view>
```

## 打包命令

| 场景 | 要求 | 命令 |
|---|---|---|
| 投稿到服务端 | 普通 zip 即可（服务端重打包） | `Compress-Archive -Path "pkg\*" -DestinationPath app.zip` |
| 容器直接读取（.nxpkg / 本地预览） | **ZIP_STORED 不压缩**、无数据描述符 | `cd pkg && zip -X -0 -r ../app.nxpkg .` |

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
| POST | `/api/v1/submissions` | 免登录投稿（multipart，见 SKILL.md） |
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
