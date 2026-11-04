---
name: nxminiapp-dev
description: 开发、调试、打包并投稿 NXMiniApp（Nintendo Switch / 桌面端小程序容器）小程序。当用户要创建、修改或修复 NXMiniApp 小程序、把微信小程序代码移植到该容器、打包 .nxpkg/.zip，或向 NXMiniApp 商店投稿时使用。
---

# NXMiniApp 小程序开发

## 容器认知（先建立正确预期）

NXMiniApp 是运行在 Nintendo Switch 与桌面端的小程序容器：页面用 WXML 描述结构、WXSS 子集描述样式，JS 由 QuickJS 常驻执行，经 Yoga 排版、NanoVG 原生绘制，**不经过 webview**。它是微信小程序的一个**可运行子集**，不是完整实现：

- 没有自定义组件、canvas、map、web-view、npm 构建、微信开发者工具
- `video` 仅在启用 MPV 的构建上可真实播放
- 未在文档列出的能力一律视为不存在，不要按微信官方文档推测

权威行为以仓库 `docs/` 为准。组件与 API 速查见 [references/cheatsheet.md](references/cheatsheet.md)。

## 硬性规则（违反直接导致安装/运行失败）

1. **包根必须直接包含 `app.json`**，zip 里不要再套一层目录（`hello/app.json` 会报「安装包缺少 app.json」）。
2. 每个页面必须同时存在 `.wxml` / `.wxss` / `.js` 三件套（`.wxss` 可以为空，但文件要存在）。
3. 只使用容器内置的 22 类标签（view、text、image、button、input、swiper、video 等）；未知标签会静默降级为 `view`。
4. 界面更新只能通过 `this.setData()`，没有双向绑定、没有 WXS。
5. `app.json` 只有 `pages`、`window.backgroundColor`、`window.navigationBarTitleText` 生效；`tabBar`、`usingComponents`、`subPackages` 等字段会被忽略。
6. 小程序 ID 必须匹配 `^[a-z0-9][a-z0-9_-]{1,63}$`。
7. 投稿限制：包 ≤ 32MB（.zip / .nxpkg），图标 png/jpg/webp ≤ 2MB。
8. 容器端安装要求 **ZIP_STORED（不压缩）** 包；投稿到服务端用普通 zip 即可（服务端解包后下载时重打包为 ZIP_STORED），但用 `NXKIT_MINIAPP_DEV` 本地预览或自行分发 `.nxpkg` 时必须用 stored 方式打包（如 `zip -X -0 -r`），否则容器拒绝。

## 目录结构

```
<package>/
├── app.json          # 必需：页面清单与窗口配置
├── app.js            # 可选：App({ onLaunch, globalData })
├── app.wxss          # 可选：全局样式（页面样式叠加其上，内联 style 最高）
├── images/           # 可选：image 组件引用的本地资源
└── pages/
    └── index/
        ├── index.wxml   # 必需
        ├── index.wxss   # 必需（可为空）
        └── index.js     # 必需，Page({...})
```

`app.json` 示例：

```json
{
  "pages": ["pages/index/index", "pages/detail/detail"],
  "window": {
    "backgroundColor": "#141518",
    "navigationBarTitleText": "我的小程序"
  }
}
```

`pages[0]` 是启动页；其余页通过 `wx.navigateTo({ url: '/pages/detail/detail?id=7' })` 打开。

## 工作流

### 1. 新建小程序

按上面的结构生成脚手架，再向用户确认小程序 ID（符合规则、有意义，如 `pixel-painter`）。参考实现可直接看仓库里的 `client/resources/miniapps/playground/`（覆盖事件、表单、轮播、存储、路由与新组件的完整示例）。

### 2. 开发页面

- WXML：`{{ }}` 插值、`wx:if` / `wx:elif` / `wx:else`、`wx:for`（建议配 `wx:key`）、`bind*` 事件绑定。
- WXSS：`rpx` 以 750 设计稿为基准；类选择器为主；页面 wxss 叠加在全局 app.wxss 之上。
- JS：每页独立 QuickJS 上下文，`Page({ data, onLoad(query), onShow, onReady, onHide, onUnload, ... })`；支持 `async/await`、Promise、定时器；全局注入 `wx` 对象（36 个真实 API，另有 18 个兼容占位）。

### 3. 本地预览

设置环境变量 `NXKIT_MINIAPP_DEV` 指向包目录，容器启动后直接打开该包，无需上架。

### 4. 打包

投稿用普通 zip 即可：

```powershell
# Windows PowerShell（投稿给服务端，服务端会重打包）
Compress-Archive -Path "<package>\*" -DestinationPath "myapp.zip" -Force
```

```bash
# 本地预览 / 自分发 .nxpkg（容器直接读取，必须 ZIP_STORED）
cd <package> && zip -X -0 -r ../myapp.nxpkg .
```

打包后自检：解压第一层必须看到 `app.json`；页面路径与 `pages` 数组完全一致。

### 5. 投稿上架（免登录，管理员审核后公开）

优先引导用户打开投稿页 `http://<host>/submit` 自助提交。用接口投稿：

```bash
curl -X POST http://<host>/api/v1/submissions \
  -F "title=我的小程序" -F "id=my-app" \
  -F "developer=你的名字" -F "category=工具" -F "version=1.0.0" \
  -F "summary=一句话简介" -F "description=详细介绍" \
  -F "icon=@icon.png" -F "package=@myapp.zip"
```

成功返回 `{ "id": "...", "message": "投稿成功，等待管理员审核上架" }`。常见失败：409（ID 被占用）、400（app.json 缺失 / 文件类型或大小不符）、429（每 IP 每小时限 10 次）。投稿后 `published=false`，商店列表不可见属正常现象，提醒用户等待审核。

## 常见错误排查

- 页面中央红色错误文字：容器不会白屏，错误信息直接画在屏幕上，按文本定位（文档《错误与排查》有对照表）。
- `安装包必须使用 ZIP_STORED`：见硬性规则 8，用 `zip -X -0` 重打。
- 样式不生效：先确认选择的是文档列出的 WXSS 子集属性与选择器；`!important`、伪类、复杂选择器不支持。
- 调用了不存在的 wx.* API：占位接口不会报错但也不做事；用 `wx.canIUse('schema')` 探测。
