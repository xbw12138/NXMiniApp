# 包结构与预览

## 1. 目录结构

```
<package>/
├── app.json          # 必需，会被装进小程序包
├── meta.json         # 可选，仅供商店目录使用，不会装进小程序包
├── icon.png          # 可选，商店封面
└── pages/
    └── index/
        ├── index.wxml
        ├── index.wxss
        └── index.js
```

- `app.json` 必需；页面路径由 `pages[0]` 决定，不带扩展名。
- 页面 `.wxml`、`.wxss`、`.js` 三个文件**全部必需**，空的 `.wxss` 也要建文件，否则加载失败。
- `app.js`、`app.wxss`、`sitemap.json`、子包、插件、自定义组件目录都不会被读取。
- `pages` 只使用第一个元素；其余页面即使存在也不会被加载，没有路由能力。
- 文件按 UTF-8 保存，不要带 BOM。
- `meta.json` 只在上架时使用，字段与约束见 [store-and-install.md](store-and-install.md)。

## 2. app.json 支持的字段

| 字段 | 行为 |
| --- | --- |
| `pages[0]` | 唯一页面路径 |
| `window.backgroundColor` | 页面根节点没有背景色时的兜底背景 |
| 其他字段 | 忽略（`navigationBar*`、`tabBar`、`usingComponents` 等都不生效） |

示例：

```json
{
  "pages": ["pages/index/index"],
  "window": { "backgroundColor": "#141518" }
}
```

## 3. 加载顺序

0. 启动器选择包目录：商店安装的 `miniapps/<id>/`，或桌面 `NXKIT_MINIAPP_DEV` 指定的目录。
1. 读取包内 `app.json`。
2. 取 `pages[0]`，拼出对应 `.wxml` / `.wxss` / `.js` 路径。
3. QuickJS 执行页面 JS，取出 `Page({ data })` 的 `data`，`JSON.stringify` 后作为渲染数据。
4. 解析 WXML、WXSS，按数据生成渲染树。
5. 首次 `draw()` 时建 Yoga 树并布局，之后窗口尺寸变化会重新计算根节点尺寸。

任何一步失败都会在页面中央显示红色错误文字，而不是白屏。

## 4. 构建与预览

桌面端构建：

```bash
cd <repo>   # client/
cmake -S . -B build_desktop -DPLATFORM_DESKTOP=ON \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DPYTHON_EXECUTABLE=$(which python3)
cmake --build build_desktop --target miniapp -j 8
```

### 4.1 商店链路（默认）

```bash
cd <NXMiniApp 仓库根>
docker compose up --build -d
```

另开终端：

```bash
cd <repo>/build_desktop/apps/miniapp
./miniapp
```

- 启动器默认连 `http://127.0.0.1:8080`，点卡片会真实下载、安装、进入。
- 可用 `NXKIT_MINIAPP_SERVER=http://192.168.1.10:8080 ./miniapp` 指向别的服务。
- 详情见 [store-and-install.md](store-and-install.md)。

### 4.2 直接预览本地包（跳过商店）

```bash
cd <repo>/build_desktop/apps/miniapp
NXKIT_MINIAPP_DEV=<repo>/resources/miniapps/<name> ./miniapp
```

- 变量指向**包目录**（里面直接是 `app.json`），不是 `app.json` 文件路径。
- 这条路径不连服务器、不安装，启动后直接进 `MiniappActivity`；按 `B` 退出程序。
- 改页面后如果只改 WXML/WXSS/JS，不需要重新构建；但若 `resources/` 被构建复制，直接预览建议仍指向仓库源目录。
- 预览窗口是 16:9，逻辑画布 1280×720。

Switch：

- 没有环境变量机制，必须走启动器 + 服务端，或预先在 `sdmc:/switch/nxkit/miniapps/<id>/` 放好包。
- 构建：

  ```bash
  DEVKITPRO=/opt/devkitpro cmake --build build_switch --target miniapp.nro
  ```

- 产物是 `build_switch/apps/miniapp/miniapp.nro`，放到 Switch 的 `sdmc:/switch/` 后从 hbmenu 启动。
- 服务器地址写在 `sdmc:/switch/nxkit/config.json` 的 `miniapp.serverUrl`（或复用 `store.serverUrl`）。
- 交叉编译已通过，但尚未在实机运行。

## 5. 调试手段

当前没有开发者工具、热重载、console 面板。可用手段只有：

- 桌面预览窗口中的红色错误文字。
- 启动日志里的 `miniapp: loading package at ...` 和 `miniapp: page data = ...`。
- 直接读源码确认行为。

启动日志示例：

```text
miniapp: loading package at <exe dir>/nxkit_data/miniapps/demo
miniapp: page data = {"appName":"NX MiniApp",...}
```

如果日志里没有 `page data`，说明 JS 阶段失败；如果连 `loading package` 都没有，说明没有进入小程序（启动器还在列表页、安装失败，或程序未启动）。

启动器的问题不写日志，直接看界面提示：列表加载失败会显示服务器地址与错误文本，空列表显示「商店里还没有小程序」。


## 6. 错误信息对照

| 错误文本 | 含义 | 处理 |
| --- | --- | --- |
| `cannot open <path>` | 文件不存在或路径错 | 检查 `app.json` 的页面路径与三件套 |
| `app.json: <detail>` | JSON 语法错 | 修 JSON，不能有注释/尾逗号 |
| `script error: <detail>` | JS 运行时异常 | 检查未定义变量、语法 |
| `index.js did not call Page()` | 没有 `Page()` 调用 | 顶层调用 `Page({...})` |
| `JSON.stringify failed: ...` | `data` 含无法序列化结构（如循环引用） | 只放纯 JSON 数据 |
| `page data: <detail>` | 序列化结果不是合法 JSON | 避免 `undefined`、函数、BigInt、循环引用 |
| `WXML: <detail>` | WXML 标签/属性未闭合 | 每位标签成对闭合，或自闭合 `/>` |
| `WXSS: missing '{' in WXSS` | 样式块语法错 | 检查选择器与花括号 |
| `WXML has no renderable root` | 页面没有可渲染节点 | 至少有一个 `view`/`text` 等元素 |

## 7. 预览检查清单

- 没有红字错误。
- 标题、正文、按钮文案都在预期位置。
- 中文字体显示正常（容器只提供常规字重，粗体是描边模拟）。
- 内容没有超出窗口；超出部分不会滚动。
- 调整窗口大小时，布局不会崩坏（根节点会重新适配）。
- 走商店链路时：列表能出卡片、封面不报错、点击能完成下载并进入、`B` 能回到启动器、再次进入时该小程序出现在最前面并带「最近使用」。

