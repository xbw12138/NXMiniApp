# 商店与安装链路

容器默认进商店式启动器：拉列表 → 点卡片 → 下载 → 安装 → 进入 → `B` 返回。本文记录这条链路的真实行为、配置位置和排查方法。

## 1. 端到端流程

```
启动器 LauncherView
  └─ GET /api/v1/miniapps                      列出已发布小程序
       └─ CoverGridPage 渲染卡片（封面 / 标题 / 分类·版本 / 最近使用徽标）
            └─ 点击卡片
                 ├─ 本地已安装且版本一致 → push MiniappActivity(packages/<id>)
                 └─ 未安装或版本不同 → GET /api/v1/miniapps/<id>/download
                      └─ 解包 ZIP_STORED → 写入 miniapps/<id>.tmp → 替换 miniapps/<id>
                           └─ push MiniappActivity(packages/<id>)
```

- 列表请求超时 15s，下载超时 30s。
- 下载进行中再点其他卡片会被拒绝，并提示「正在下载 …，请稍候」。
- 安装成功后先写最近使用，再进入页面；`MiniappView` 的 `B` 执行 `popActivity()`，回到启动器。
- 启动器每次重新出现都会重排卡片，把最近使用的置顶并加「最近使用」徽标；列表本身不会自动重新请求，需要点「刷新」或下拉刷新。

## 2. 服务端接口

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/v1/miniapps` | 列表；可选 `q`（标题/简介/ID 模糊匹配）、`category` 精确过滤 |
| GET | `/api/v1/miniapps/{id}` | 详情 |
| GET | `/api/v1/miniapps/{id}/download` | 下载 `.nxpkg`，始终 `ZIP_STORED` |
| GET | `/api/v1/miniapps/{id}/icon` | 可选封面图片 |

列表返回：

```json
{
  "items": [
    {
      "id": "hello",
      "title": "你好小程序",
      "summary": "一个通过商店接口下载安装的最小示例",
      "description": "用于验证整条链路。",
      "category": "示例",
      "developer": "NXKit",
      "icon_url": "/api/v1/miniapps/hello/icon",
      "version": "0.1.0",
      "size": 1234,
      "published": true,
      "updated_at": 1762000000000,
      "download_url": "/api/v1/miniapps/hello/download"
    }
  ],
  "updated_at": 1762000000000
}
```

- `size` 是 `package/` 目录的字节数，由服务端即时计算。
- `icon_url`、`download_url` 可能是相对路径，客户端会拼成绝对地址。
- `published: false` 的条目不会出现在列表里。
- 元数据在 MySQL，包体在磁盘；后台改动立即生效，**不需要重启服务**。

## 3. 存储结构

服务端 = MySQL（元数据）+ 磁盘目录（包体与图标，Docker 卷 `server-data`，容器内 `/data`）：

```
/data/miniapps/<id>/
├── package/    # 真正下发的文件：app.json、pages/**
└── icon.png    # 可选封面
```

MySQL `miniapps` 表字段：`id,title,summary,description,category,developer,version,icon,published,downloads,created_at,updated_at`（时间戳为毫秒）。

种子目录 `client/resources/miniapps/<id>/` 的 `meta.json` 只在**首次启动**（表为空）时导入，字段：

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| `title` | 目录名 | 卡片标题 |
| `summary` | 空 | 简介，参与搜索 |
| `description` | 空 | 详情描述 |
| `category` | `示例` | 分类；启动器显示为「分类 · v版本 · 大小」 |
| `developer` | 空 | 开发者 |
| `version` | `1.0.0` | 与本地 `.version` 比较，决定是否需要重新下载 |
| `icon` | 空 | 有 `icon.png` 时自动登记 |
| `published` | `true` | 为 `false` 时不下发 |

## 4. 上架与更新

**管理后台（推荐）**：浏览器打开 `http://127.0.0.1:8080/admin`，令牌默认 `nxkit-store-admin`（环境变量 `NXKIT_ADMIN_TOKEN` 覆盖）。在「小程序」页：

1. 新建小程序（ID 规则同上），填写标题/简介/分类/开发者/版本；
2. 上传图标（png/jpg/jpeg/webp）；
3. 上传安装包（`.nxpkg` 或 `.zip`，根目录必须含 `app.json`，服务端安全解包后替换 `package/`）；
4. 更新内容时递增版本号并重新上传包；用发布开关控制上下架。

也可以用管理 API：

```bash
curl -X POST -H "Authorization: Bearer nxkit-store-admin" \
  -F "file=@hello.zip" -F "version=0.2.0" \
  http://127.0.0.1:8080/api/v1/admin/miniapps/hello/package
```

**种子目录**：改 `client/resources/miniapps` 只在首次 `docker compose up --build` 时生效（表非空就跳过）；日常更新请走管理后台。

客户端没有「更新」按钮；只要服务端 `version` 变化，下次点击该卡片就会重新下载并覆盖安装。

## 5. 客户端配置与本地路径

服务器地址优先级（[main.cpp](../../apps/miniapp/source/main.cpp)）：

1. 环境变量 `NXKIT_MINIAPP_SERVER`（桌面开发用）
2. `config.json` 的 `miniapp.serverUrl`
3. `config.json` 的 `store.serverUrl`（与商店应用共用）
4. 仅桌面：兜底 `http://127.0.0.1:8080`；Switch 没有兜底，必须配置

`config.json` 位置：

- 桌面：`<build_desktop>/apps/miniapp/nxkit_data/config.json`
- Switch：`sdmc:/switch/nxkit/config.json`

本地数据：

| 内容 | 桌面 | Switch |
| --- | --- | --- |
| 安装目录 | `<exe>/nxkit_data/miniapps/<id>/` | `sdmc:/switch/nxkit/miniapps/<id>/` |
| 版本标记 | `<id>/.version` | 同左 |
| 最近使用 | `config.json` → `miniapp.recent` | 同左 |

`miniapp.recent` 是字符串数组，最新在前，最多 8 条；进入小程序时写入，保存失败不会阻止进入。

## 6. 安装包约束

客户端 [package_manager.cpp](../../apps/miniapp/source/install/package_manager.cpp) 自行解包，不支持 deflate：

- 只接受 `ZIP_STORED`（`method == 0` 且压缩前后大小相等）。
- 不接受带数据描述符（flag bit 3）的条目。
- 路径必须是相对路径，不能以 `/` 开头，不能包含 `..`。
- 包内至少要有一个文件，且根目录必须有 `app.json`。
- 解包先写 `<id>.tmp`，校验通过后删除旧目录再 `rename`，失败时清掉临时目录。

因此不要手工用普通 zip 工具重新打包；新包一律通过管理后台/API 上传（服务端解包保存），下载时由服务端按需生成 ZIP_STORED。

## 7. 排障

| 现象 | 检查 |
| --- | --- |
| 启动器一直「正在连接商店…」 | 服务是否启动；地址是否可达；15s 内是否返回 |
| 显示服务器地址 + 连接错误 | 用 `curl http://<host>:8080/api/v1/miniapps` 验证；检查 `config.json` |
| 列表为空 | MySQL 里是否有 published 记录（管理后台可见）；首次启动种子是否导入成功（服务端日志） |
| 卡片无封面 | 是否上传过图标；`/icon` 是否 404；客户端会退回默认封面 |
| 点击后提示下载失败 | 下载接口是否 404；包目录是否存在；服务端日志 |
| 提示必须 ZIP_STORED | 包不是服务端生成的（服务端用 Go `archive/zip` 的 Store + CreateRaw 打包，无数据描述符）；手工 zip 一律不行 |
| 提示缺少 app.json | 包根层级错了，`app.json` 必须直接在 `package/` 下 |
| 进入后仍显示旧页面 | 版本号没递增，客户端认为已安装，直接进了旧目录 |
| 最近使用不更新 | `config.json` 是否可写；`miniapp.recent` 是否被手工改成了非数组 |
