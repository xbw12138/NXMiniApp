# API 与能力状态

状态说明：

- **已实现**：行为与描述一致，可直接使用。
- **部分实现**：外观或基础行为可用，但缺少微信对应能力。
- **占位**：只渲染外观，没有真实功能。
- **未支持**：完全不生效或不存在。
- **计划**：尚未实现；不要在代码里假设它可用。

## 0. 容器能力（商店与安装）

这部分是容器外壳的能力，不是小程序内可调用的 `wx.*`。

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 商店列表 | 已实现 | 启动器请求 `/api/v1/miniapps`，`CoverGridPage` 渲染卡片 |
| 远程封面 | 已实现 | `icon_url` 走 HTTP 加载；为空时退回默认背景图 |
| 下载与安装 | 已实现 | 下载 `.nxpkg`，客户端解 ZIP_STORED，安装到 `miniapps/<id>/` |
| 版本检查 | 部分实现 | 比较服务端 `version` 与本地 `.version`；没有独立「更新」界面或红点 |
| 进入与返回 | 已实现 | 点击进入 `MiniappActivity`，`B` 返回启动器 |
| 最近使用 | 已实现 | `config.json` 的 `miniapp.recent`，最多 8 条，置顶并显示徽标 |
| 手动刷新 | 已实现 | 顶部「刷新」按钮与网格下拉刷新 |
| 错误/空态 | 已实现 | 连接失败、空列表都有提示，不会白屏 |
| 搜索 / 分类筛选 UI | 未支持 | 服务端支持 `q`、`category`，启动器没有入口 |
| 排序 / 推荐位 | 未支持 | 固定按最近使用 + 服务端返回顺序 |
| 管理后台 | 已实现 | `http://<host>:8080/admin`（Vue SPA）：令牌登录、概览、商店应用管理、小程序管理（图标/包上传、发布开关） |
| 小程序内 `wx.*` | 部分实现 | 36 个可用接口 + 18 个兼容占位，见第 3 节 |

## 1. JS 运行时

| 项 | 状态 | 说明 |
| --- | --- | --- |
| QuickJS 执行页面脚本 | 已实现 | 每个页面独立上下文，打开时执行一次并常驻 |
| ES2020 语法 | 已实现 | 由 QuickJS 版本决定 |
| `Page(options)` | 已实现 | 注册页面对象；`data`、生命周期与方法 |
| `App(options)` / `getApp()` | 已实现 | 应用级对象与 `onLaunch` / `onShow` / `onHide` |
| `getCurrentPages()` | 已实现 | 返回当前页面路径数组 |
| `Component` | 未支持 | 不存在 |
| `setData` | 已实现 | 递归合并进 `this.data`，支持点路径，下一帧重绘 |
| 生命周期 | 已实现 | `onLoad(query)` / `onShow` / `onReady` / `onHide` / `onUnload` |
| 事件对象 | 已实现 | `type` / `timeStamp` / `target` / `currentTarget` / `dataset` / `detail` |
| `console` | 已实现 | `log` / `info` / `warn` / `error` 写入宿主日志 |
| `setTimeout` / `setInterval` | 已实现 | 由容器每帧推进；页面销毁时清理 |
| Promise / async 回调 | 已实现 | 每次回调后执行微任务队列 |
| `require` / ES Module | 未支持 | 单文件脚本 |
| 多个 `Page()` 调用 | 已实现 | 以最后一次为准 |

约束：

- `data` 必须可 JSON 序列化；函数、循环引用会丢失或报错。
- 页面 JS 异常会显示在页面上，页面不会渲染。
- 没有热更新，修改后要重启预览（资源文件在启动时读取）。

## 2. 组件

| 组件 | 状态 | 实际行为 |
| --- | --- | --- |
| `view` | 已实现 | 盒子、背景、边框、圆角、Flex、绝对定位、阴影；`bindtap` |
| `text` | 已实现 | 插值、自动换行、对齐、行高、粗体模拟 |
| `image` | 部分实现 | 加载包内本地图片；支持 `scaleToFill` / `aspectFit` / `aspectFill`；远程 URL 显示占位 |
| `button` | 已实现 | 绿色胶囊外观、居中文字、`bindtap` |
| `scroll-view` | 部分实现 | `scroll-y` / `scroll-x` 裁剪与焦点自动滚动；无手势、滚轮与 `bindscroll` |
| `swiper` / `swiper-item` | 部分实现 | `autoplay` / `interval` 自动轮播与指示点；无 `bindchange`、纵向与手势 |
| `input` / `textarea` | 部分实现 | 系统输入法、`value` / `placeholder`、`bindinput` / `bindconfirm`；textarea 仍是单行 |
| `switch` / `checkbox` / `radio` | 已实现 | 原生绘制、A 切换、`bindchange`；radio 支持 `name` 分组 |
| `slider` | 已实现 | `min` / `max` / `step` / `value`，方向键左右调整，`bindchange` |
| `navigator` | 已实现 | `url` 与 `open-type`（redirect / reLaunch），默认 navigateTo |
| `progress` | 已实现 | `percent` / `activeColor` / `backgroundColor` |
| `rich-text` | 部分实现 | 读取 `nodes`，剥离标签后按纯文本渲染 |
| `icon` | 已实现 | `type` / `size`，成功、警告、取消、等待、搜索、下载 |
| `video` | 部分实现 | `src` / `poster` / `autoplay` / `loop` / `muted` / `controls` / `object-fit`，点击或 A 播放暂停；MPV 构建可真实播放 |
| `form` / `label` / `checkbox-group` / `radio-group` | 部分实现 | 容器语义，正常排版；不汇总提交 |
| `picker` / `picker-view` | 未支持 | 退化为 `view` |
| `canvas` / `audio` / `map` / `web-view` | 未支持 | 退化为 `view` |
| 自定义组件 / `usingComponents` | 未支持 | 组件目录不会被读取 |
| 事件绑定 | 已实现 | `bindtap`（含鼠标/触摸命中与冒泡）/ `bindchange` / `bindinput` / `bindconfirm` / video 的 `bindplay` 等 |
| `hidden` / `wx:key` | 未支持 | `hidden` 不生效；`wx:key` 无 diff 作用 |

## 3. wx.* API

可用接口：

| 分类 | API | 状态 |
| --- | --- | --- |
| 网络 | `wx.request` | 已实现（GET query / 其他方法 JSON body） |
| 界面 | `wx.showToast`、`wx.showLoading`、`wx.hideLoading`、`wx.showModal` | 已实现 |
| 导航栏 | `wx.setNavigationBarTitle` | 已实现 |
| 路由 | `wx.navigateTo`、`wx.navigateBack`、`wx.redirectTo`、`wx.reLaunch` | 已实现 |
| 数据缓存 | `wx.getStorageSync`、`wx.setStorageSync`、`wx.removeStorageSync`、`wx.clearStorageSync`、`wx.getStorageInfoSync` 及对应异步版本 | 已实现 |
| 系统 | `wx.getSystemInfoSync`、`wx.getSystemInfo`、`wx.getWindowInfo`、`wx.getDeviceInfo`、`wx.getAppBaseInfo` | 已实现 |
| 启动参数 | `wx.getLaunchOptionsSync`、`wx.getEnterOptionsSync` | 已实现 |
| 设备 | `wx.getNetworkType`、`wx.getMenuButtonBoundingClientRect` | 已实现 |
| 剪贴板 | `wx.setClipboardData`、`wx.getClipboardData` | 已实现（进程内） |
| 媒体 | `wx.createVideoContext` | 已实现（play / pause / stop / seek；依赖 MPV 构建） |
| 更新 | `wx.getUpdateManager` | 占位（返回空实现对象） |
| 工具 | `wx.canIUse`、`wx.nextTick` | 已实现 |
| 振动 | `wx.vibrateShort` | 占位（宿主无振动接口） |

兼容占位（调用不报错、无实际效果）：

`setNavigationBarColor`、`showNavigationBarLoading`、`hideNavigationBarLoading`、
`setKeepScreenOn`、`setPreferredFramesPerSecond`、`reportAnalytics`、`reportEvent`、
`onNetworkStatusChange`、`onAppShow`、`onAppHide`、`onError`、`onUnhandledRejection`、
`pageScrollTo` 及对应 `off*` 接口。

尚未支持（调用得到 `undefined`）：

`downloadFile`、`uploadFile`、`login`、`getUserInfo`、`requestPayment`、`chooseImage`、
`previewImage`、`createInnerAudioContext`、`getFileSystemManager`、`scanCode`、`getLocation`、
`openLocation`、`createCanvasContext`、`createSelectorQuery`、`cloud`、`shareAppMessage`、
`subscribeMessage` 等。

### 用户要求这些 API 时怎么办

1. 明确说明当前版本没有该能力，不要写“伪调用”代码。
2. 能用已支持接口或静态数据替代的，先改写成可运行实现。
3. 确实必须新增运行时能力时，按第 5 节流程扩展，并同步更新本文件与 `docs/api/index.html`。

## 4. 与微信小程序的差异

| 维度 | 微信 | NXKit 当前 |
| --- | --- | --- |
| 入口 | 微信内的发现页 / 搜索 | 独立容器，先进入商店式启动器 |
| 包获取 | 微信后台分发，自动更新 | 从 FastAPI 目录下载 `.nxpkg`，点击时按版本安装 |
| 最近使用 | 有 | 有，最多 8 条，置顶显示 |
| 渲染 | webview | Yoga + NanoVG 原生绘制 |
| 样式 | 完整 WXSS | 子集，见 [wxss.md](wxss.md) |
| 选择器优先级 | 有 | 无，按源码顺序覆盖 |
| 页面 | 多页面 + 路由 | 多页面 + 页面栈 + query |
| 交互 | 触摸、滚动 | 手柄焦点导航；桌面键盘方向键 / Enter / Backspace |
| 数据更新 | `setData` 增量更新 | `setData` 合并后整树重建 |
| 组件 | 完整组件库 | 22 类内置标签，无自定义组件 |
| 图片 | 真实加载、网络图 | 包内本地图片；网络图占位 |
| 事件 | 完整事件系统 | tap / change / input / confirm，无冒泡捕获 |
| 自定义组件 | 支持 | 不支持 |
| `wx.*` | 完整 API | 36 个可用 + 18 个兼容占位 |
| 单位 | `rpx` 按屏幕宽换算 | 固定 `640/750`，与窗口宽度无关 |
| 文本 | 无行数上限 | 自动换行，单段建议 ≤ 64 行 |
| 字体 | 可加载自定义字体 | 仅内置常规字体，粗体为模拟 |
| 横竖屏 | 支持 | 固定 16:9 横屏 |

## 5. 扩展运行时的流程

只有在用户明确要求“增加运行时能力”时才执行：

1. 在 `apps/miniapp` 内实现，不要修改 `library/borealis` 的通用行为。
2. JS 绑定统一在 [js_runtime.cpp](../../apps/miniapp/source/js_runtime.cpp)；新 API 优先挂在 `wx.nx.*` 下。
3. 同步更新：本文件的状态表、[SKILL.md](../SKILL.md) 的硬约束、`docs/` 对应页面，以及一个可运行的示例包。
4. 桌面构建通过并实际预览截图确认。
5. 未在 Switch 实机验证的能力，必须注明“仅桌面验证”。

禁止：

- 不要把未实现的能力写成“已支持”。
- 不要为了对齐微信而伪造返回值（例如返回假系统信息）。
- 不要在文档里承诺尚不存在的 API。
