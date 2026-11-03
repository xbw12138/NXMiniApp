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
| 小程序内 `wx.*` | 未支持 | 详见第 3 节 |

## 1. JS 运行时

| 项 | 状态 | 说明 |
| --- | --- | --- |
| QuickJS 执行页面脚本 | 已实现 | 启动时执行一次 |
| ES2020 语法 | 已实现 | 由 QuickJS 版本决定 |
| 全局 `Page({ data })` | 已实现 | 只读取 `data`，`JSON.stringify` 后交给渲染器 |
| `wx` 全局对象 | 未支持 | 不存在；引用会抛 `ReferenceError` |
| `App` / `getApp` / `Component` | 未支持 | 不存在 |
| `setData` | 未支持 | 页面数据不可变 |
| 生命周期方法（`onLoad` 等） | 未支持 | 函数属性会被 JSON 序列化丢弃 |
| `console` | 未支持 | 没有注入 |
| `setTimeout` / `setInterval` | 未支持 | 没有事件循环 |
| Promise / async 回调 | 部分实现 | 语言层可用，但没有事件循环，脚本返回后不会继续执行 |
| `require` / ES Module | 未支持 | 单文件脚本 |
| 多个 `Page()` 调用 | 已实现 | 以最后一次为准 |

约束：

- `data` 必须是可 JSON 序列化的纯数据；函数、`undefined`、循环引用会丢失或报错。
- 脚本异常会直接显示在页面上，页面不会渲染。
- 没有热更新，修改后要重启预览。

## 2. 组件

| 组件 | 状态 | 实际行为 |
| --- | --- | --- |
| `view` | 部分实现 | 盒子、背景、边框、圆角、flex 布局；无事件、无滚动、无 `hidden` |
| `text` | 部分实现 | 文本换行、对齐、行高、粗体模拟；无 `selectable`、`space`、嵌套样式 |
| `image` | 占位 | 只画占位块与 `src` 文件名；不加载图片、不支持 `mode` |
| `button` | 占位 | 绿色胶囊外观与居中文字；无点击、无 `open-type`、无 loading |
| `scroll-view` | 未支持 | 退化为 `view`，不会滚动 |
| `swiper` / `swiper-item` | 未支持 | 退化为 `view` |
| `input` / `textarea` | 未支持 | 退化为 `view`，无法输入 |
| `picker` / `checkbox` / `radio` / `switch` / `slider` | 未支持 | 退化为 `view` |
| `form` | 未支持 | 退化为 `view` |
| `navigator` | 未支持 | 退化为 `view`，无路由 |
| `rich-text` | 未支持 | 退化为 `view` |
| `canvas` / `video` / `audio` / `map` / `web-view` | 未支持 | 退化为 `view` |
| 自定义组件 / `usingComponents` | 未支持 | 组件目录不会被读取 |
| 事件绑定（`bindtap` 等） | 未支持 | 属性被忽略 |
| `hidden` / `wx:key` | 未支持 | `hidden` 不生效；循环不复用 |

## 3. wx.* API

**当前容器没有注入 `wx` 对象，以下全部为「未支持」。**

| 分类 | 典型 API | 状态 |
| --- | --- | --- |
| 系统 | `wx.getSystemInfoSync`、`wx.getWindowInfo` | 未支持 |
| 界面 | `wx.showToast`、`wx.showModal`、`wx.showLoading` | 未支持 |
| 导航栏 | `wx.setNavigationBarTitle`、`wx.setNavigationBarColor` | 未支持 |
| 路由 | `wx.navigateTo`、`wx.redirectTo`、`wx.reLaunch`、`wx.navigateBack` | 未支持 |
| 数据缓存 | `wx.getStorageSync`、`wx.setStorageSync`、`wx.clearStorage` | 未支持 |
| 网络 | `wx.request`、`wx.downloadFile`、`wx.uploadFile` | 未支持 |
| 媒体 | `wx.createInnerAudioContext`、`wx.chooseImage`、`wx.previewImage` | 未支持 |
| 文件 | `wx.getFileSystemManager` | 未支持 |
| 设备 | `wx.vibrateShort`、`wx.getBatteryInfo`、`wx.getNetworkType` | 未支持 |
| 扫码/蓝牙/NFC | `wx.scanCode`、`wx.openBluetoothAdapter`、`wx.startHCE` | 未支持 |
| 位置/地图 | `wx.getLocation`、`wx.openLocation` | 未支持 |
| 开放接口 | `wx.login`、`wx.requestPayment`、`wx.shareAppMessage`、`wx.subscribeMessage` | 未支持 |
| 云开发 | `wx.cloud` | 未支持 |
| 画布 | `wx.createCanvasContext`、`wx.createSelectorQuery` | 未支持 |

### 用户要求这些 API 时怎么办

1. 明确说明当前版本没有该能力，不要写“伪调用”代码。
2. 能用静态数据或预计算替代的，先在 JS 里算出结果再渲染。
3. 确实必须新增运行时能力时，按第 5 节流程扩展，并同步更新本文件。

## 4. 与微信小程序的差异

| 维度 | 微信 | NXKit 当前 |
| --- | --- | --- |
| 入口 | 微信内的发现页 / 搜索 | 独立容器，先进入商店式启动器 |
| 包获取 | 微信后台分发，自动更新 | 从 FastAPI 目录下载 `.nxpkg`，点击时按版本安装 |
| 最近使用 | 有 | 有，最多 8 条，置顶显示 |
| 渲染 | webview | Yoga + NanoVG 原生绘制 |
| 样式 | 完整 WXSS | 子集，见 [wxss.md](wxss.md) |
| 选择器优先级 | 有 | 无，按源码顺序覆盖 |
| 页面 | 多页面 + 路由 | 单页面，无路由 |
| 交互 | 触摸、滚动 | 无触摸、无滚动、无焦点导航 |
| 数据更新 | `setData` 增量更新 | 启动时一次性求值 |
| 组件 | 完整组件库 | 4 个基础组件，其中 2 个是占位 |
| 图片 | 真实加载 | 占位块 |
| 事件 | 完整事件系统 | 无 |
| 自定义组件 | 支持 | 不支持 |
| `wx.*` | 完整 API | 无 |
| 单位 | `rpx` 按屏幕宽换算 | 固定 `640/750`，与窗口宽度无关 |
| 文本 | 无行数上限 | 单段最多 64 行 |
| 字体 | 可加载自定义字体 | 仅内置常规字体，粗体为模拟 |
| 横竖屏 | 支持 | 固定 16:9 横屏 |

## 5. 扩展运行时的流程

只有在用户明确要求“增加运行时能力”时才执行：

1. 在 `apps/miniapp` 内实现，不要修改 `library/borealis` 的通用行为。
2. JS 绑定统一在 [js_runtime.cpp](../../apps/miniapp/source/js_runtime.cpp)；新 API 优先挂在 `wx.nx.*` 下。
3. 同步更新：本文件的状态表、[SKILL.md](../SKILL.md) 的硬约束、以及一个可运行的示例包。
4. 桌面构建通过并实际预览截图确认。
5. 未在 Switch 实机验证的能力，必须注明“仅桌面验证”。

禁止：

- 不要把未实现的能力写成“已支持”。
- 不要为了对齐微信而伪造返回值（例如返回假系统信息）。
- 不要在文档里承诺尚不存在的 API。
