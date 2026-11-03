---
name: nx-miniapp-dev
description: "开发、上架、预览和排查 NXKit Switch 小程序容器（apps/miniapp）的 WXML/WXSS/JS 包。用于新建或修改 resources/miniapps 下的小程序、解释页面渲染异常、核对组件/CSS/wx.* API 是否可用，以及配置商店服务与安装链路。只描述当前已实现能力，不要臆造微信 API。"
---

# NXKit 小程序开发

## 先看现状

- 容器入口：[apps/miniapp](../../apps/miniapp)。启动后先进入**商店式启动器**：从服务端拉取小程序列表，点击卡片后下载、安装并进入；`B` 返回启动器。
- 小程序包由 Go 服务提供（仓库根 `server/`，Gin + GORM + MySQL），安装到本地后由容器读取。
- 页面仍由 `WXML → Yoga 布局 → NanoVG 绘制` 完成，不经过 webview。
- 运行时**没有注入任何 `wx.*` API**；单页面、静态一次性渲染，无事件、无滚动。
- 小程序样式使用 WXSS 的**子集**，不是完整 CSS。
- 所有能力以源码为准；文档与源码不一致时，改代码或改文档，不要按微信文档猜测。
  关键源码：
  - [launcher_view.cpp](../../apps/miniapp/source/pages/launcher_view.cpp)：商店列表、下载/安装/进入、最近使用
  - [miniapp_api.cpp](../../apps/miniapp/source/api/miniapp_api.cpp)：目录接口与下载
  - [package_manager.cpp](../../apps/miniapp/source/install/package_manager.cpp)：ZIP_STORED 解包与安装
  - [miniapp_view.cpp](../../apps/miniapp/source/miniapp_view.cpp)：标签、指令、`{{}}`、渲染
  - [wxss.cpp](../../apps/miniapp/source/wxss.cpp) / [style.cpp](../../apps/miniapp/source/style.cpp)：选择器与 CSS 属性
  - [js_runtime.cpp](../../apps/miniapp/source/js_runtime.cpp)：JS 全局对象
  - [layout.cpp](../../apps/miniapp/source/layout.cpp)：Yoga 布局
  - 参考包：[demo](../../resources/miniapps/demo)、[hello](../../resources/miniapps/hello)

## 按需阅读

| 文件 | 何时读 |
| --- | --- |
| [references/store-and-install.md](references/store-and-install.md) | 上架小程序、对接商店服务、排查下载/安装/最近使用 |
| [references/package-and-preview.md](references/package-and-preview.md) | 建包、构建、预览、定位报错 |
| [references/wxml.md](references/wxml.md) | 写页面结构、`wx:for` / `wx:if` / `{{}}` |
| [references/wxss.md](references/wxss.md) | 写样式前确认属性、单位、选择器是否支持 |
| [references/api-status.md](references/api-status.md) | 用户要求 `wx.*`、组件、交互、网络、存储时 |

## 工作流

1. 在 `resources/miniapps/<id>/` 下建包，或直接复制 `resources/miniapps/hello`。
   - `<id>` 必须匹配 `^[a-z0-9][a-z0-9_-]{1,63}$`（2–64 个字符）。
   - 包内至少要有 `app.json`；建议再放 `meta.json`（上架信息）与 `icon.png`（封面）。
2. 保证页面三件套都存在：`index.wxml`、`index.wxss`、`index.js`（**空的 wxss 也必须建文件**）。
3. 配置并构建桌面端（本目录就是 `client/`，cmake 需要额外标志）：

   ```bash
   cmake -S . -B build_desktop -DPLATFORM_DESKTOP=ON \
     -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DPYTHON_EXECUTABLE=$(which python3)
   cmake --build build_desktop --target miniapp -j 8
   ```

4. 启动商店服务（Docker，MySQL + 服务端 + 管理后台）：

   ```bash
   cd <NXMiniApp 仓库根>   # client/ 的上一级
   docker compose up --build -d
   ```

   首次启动自动建表，并把 `client/resources/miniapps` 下的示例包导入为种子数据。
   管理后台在 `http://127.0.0.1:8080/admin`，默认令牌 `nxkit-store-admin`。

5. 预览：
   - **商店链路（默认）**：直接运行 `./miniapp`，启动器会连 `http://127.0.0.1:8080`（桌面兜底）。
   - **跳过商店，直接看某个本地包**（改页面时最省事）：

     ```bash
     cd <repo>/build_desktop/apps/miniapp
     NXKIT_MINIAPP_DEV=<repo>/resources/miniapps/<id> ./miniapp
     ```

   - 资源从 `build_desktop/apps/miniapp/resources`（构建时复制）读取，所以先构建再运行。
   - `B` 从页面返回启动器；从启动器再按 `B` 退出容器。`A` 只打日志，不是交互按钮。

6. 检查：页面是否无红色错误文字、文案是否完整、是否有内容超出 16:9 画面。
   当前没有滚动，超出画面只能被裁掉或压出边界。

## 最小可用包

`app.json`

```json
{
  "pages": ["pages/index/index"],
  "window": { "backgroundColor": "#141518" }
}
```

`pages/index/index.wxml`

```xml
<view class="page">
  <text class="title">{{title}}</text>
  <button class="start">开始</button>
</view>
```

`pages/index/index.wxss`

```css
.page {
  background-color: #141518;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}

.title {
  color: #F5F6F7;
  font-size: 40rpx;
  font-weight: bold;
}
```

`pages/index/index.js`

```js
Page({
  data: { title: "Hello NX MiniApp" }
})
```

上架用的 `meta.json`（可选，但不写就只显示 ID，分类为「其他」）：

```json
{
  "title": "演示小程序",
  "summary": "一句话简介",
  "description": "详细介绍",
  "category": "示例",
  "developer": "NXKit",
  "version": "0.1.0",
  "published": true
}
```

`meta.json` 不会被装进小程序包，只供商店目录读取；有 `icon.png` 时把它放到包目录根部，`seed` 会一并复制。

## 硬约束（写之前逐条确认）

- 只有 4 类标签有意义：`view`、`text`、`image`、`button`；其他标签一律退化为 `view`。
- `{{}}` 只支持**点路径取值**，不支持表达式、三元、函数调用、索引、运算。
- 只支持 `wx:for`（含 `wx:for-item` / `wx:for-index`）和 `wx:if`；没有 `wx:elif` / `wx:else` / `wx:key`。
- JS 只有全局 `Page({ data })`；没有 `wx`、`App`、`Component`、`getApp`、`console`、`setData`、定时器、模块系统。
- 页面只在启动时渲染一次；改数据必须重新进入小程序。
- `image` 只画占位块与文件名，**不会加载真实图片**。
- `button` 只负责画绿底文字，**没有点击事件**。
- 没有滚动、没有触摸、没有焦点导航、没有输入法、没有多页面路由。
- 样式只支持 WXSS 子集：无 `position`、`display`、`flex` 缩放、`opacity`、`overflow`、`box-shadow`、`transform`、`transition`、`background-image`、渐变、伪类、组合选择器。
- 不要按手机 375pt 宽度设计：逻辑画布是 1280×720，`1rpx = 640/750 ≈ 0.853` 逻辑像素，`750rpx ≈ 640`，**不是整屏宽**。
- 单段文字最多绘制 64 行，超出部分不会显示。
- 商店安装包必须是 **ZIP_STORED**（服务端已按此打包）；包根必须有 `app.json`；不允许绝对路径或 `..`。
- 安装目录按 ID 存放，同一 ID 新版本会覆盖旧目录；最近使用只记录 ID，最多 8 条。

## 写完后自检

1. `app.json` 的 `pages[0]` 是否与页面文件路径一致，且不带扩展名？
2. 三件套是否都存在？
3. `index.js` 是否调用了 `Page()`，且 `data` 能被 `JSON.stringify`？
4. WXML 里是否出现了表达式、`wx:*` 以外的指令、或非四种标签？
5. 用到的每个 CSS 属性是否在 [wxss.md](references/wxss.md) 的白名单里？
6. 内容在 1280×720 内是否能放下？
7. 要上架的话：`meta.json` 的 `version` 是否已递增？`published` 是否为 `true`？`id` 是否合法且与目录名一致？

## 常见故障

| 现象 | 原因 |
| --- | --- |
| 启动器一直显示「正在连接商店…」 | 服务没起、地址不可达，或卡在 15s 超时 |
| 启动器显示「请确认商店服务已启动…」 | `miniapp.serverUrl` / `store.serverUrl` 配错，或服务端报错 |
| 列表为空「商店里还没有小程序」 | `data/miniapps` 下没有已发布的包，先跑 `seed_miniapps.py` |
| 卡片没有封面 | `meta.json` 的 `icon` 为空或图标文件不存在，会退回默认图 |
| `下载失败: ...` | 下载接口 404/超时；确认 `/api/v1/miniapps/{id}/download` 可访问 |
| `安装失败: 安装包必须使用 ZIP_STORED` | 用了别人的普通 zip；服务端打包或重新生成 |
| `安装失败: 安装包缺少 app.json` | 包根目录缺 `app.json` |
| 屏幕中央红字 `cannot open ...` | 路径写错或缺少文件（最常见是缺 `.wxss`） |
| `index.js did not call Page()` | 脚本没有调用 `Page()`，或调用发生在错误之后 |
| `script error: ...` | QuickJS 抛异常；检查未定义全局变量、语法错误 |
| `WXML: missing '}'` / `WXSS: missing '{'` | 手写解析器要求成对闭合，不能省略 |
| 文字不显示 | 文本被放进了自闭合标签、文本为空、或颜色与背景同色 |
| 布局挤在一起 | 父节点缺 `flex-direction: row`，或用了未支持的 `flex` / `display` |
| 页面超出画面 | 没有滚动能力，需要减少内容或缩小尺寸 |

## 扩展边界

用户要求的能力如果不在本文档中：

1. 先明确说明“当前容器未实现”，不要用占位实现伪装可用。
2. 只有在用户明确要求扩展运行时，才改 `apps/miniapp` 的 C++ 代码，并同步更新本 skill。
3. 新增能力应优先使用 `wx.nx.*` 扩展命名空间，避免与微信语义混淆；差异必须记录在 [api-status.md](references/api-status.md)。
