# NXMiniApp

Switch 小程序容器项目，独立于 NXKit 维护。

```
NXMiniApp/
├── client/            # Switch / 桌面客户端（borealis 原生渲染）
│   ├── apps/miniapp/  # 容器应用：商店式启动器 + 小程序运行时
│   ├── core/          # NXKit 核心（HTTP、配置、UI 组件）
│   ├── library/       # borealis / QuickJS / Yoga 等
│   └── resources/     # 字体、图标、示例小程序包
├── server/            # Go + Gin + GORM + MySQL 服务端与管理后台
└── docker-compose.yml # MySQL + 服务端一键启动
```

## 服务端（Docker，推荐）

```bash
cd NXMiniApp
docker compose up --build -d
```

- API：`http://127.0.0.1:8080/api/v1/…`
- 管理后台：`http://127.0.0.1:8080/admin`
- 管理令牌：环境变量 `NXKIT_ADMIN_TOKEN`（默认 `nxkit-store-admin`）
- 首次启动自动建表，并把 `client/resources/miniapps` 下的示例小程序写入 MySQL 与数据卷（仅在小程序表为空时执行）。

数据：MySQL 存元数据，`server-data` 卷存安装包/图标/截图。

构建镜像时默认走国内源（npm → npmmirror、Go → goproxy.cn、Alpine → aliyun），可通过 `NPM_REGISTRY` / `GOPROXY_URL` / `APK_MIRROR` 构建参数覆盖：

```bash
docker compose build \
  --build-arg NPM_REGISTRY=https://registry.npmmirror.com \
  --build-arg GOPROXY_URL=https://goproxy.cn,direct
```

## 客户端

桌面预览（需要 CMake，borealis 依赖见 client/library）：

```bash
cd client
cmake -S . -B build_desktop -DPLATFORM_DESKTOP=ON \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DPYTHON_EXECUTABLE=$(which python3)
cmake --build build_desktop --target miniapp -j 8
cd build_desktop/apps/miniapp && ./miniapp
```

- 启动后进商店式启动器，默认连 `http://127.0.0.1:8080`；可用 `NXKIT_MINIAPP_SERVER` 覆盖，或写进 `nxkit_data/config.json` 的 `miniapp.serverUrl`。
- 点卡片下载安装并进入；`B` 返回启动器；最近使用置顶。
- 跳过商店直接预览本地包：`NXKIT_MINIAPP_DEV=<包目录> ./miniapp`（仅桌面）。

Switch 真机（需要 devkitPro）：

```bash
cd client
DEVKITPRO=/opt/devkitpro cmake -S . -B build_switch -DPLATFORM_SWITCH=ON \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DPYTHON_EXECUTABLE=$(which python3)
cmake --build build_switch --target miniapp.nro -j 8
```

把 `build_switch/apps/miniapp/miniapp.nro` 放到 `sdmc:/switch/`，服务器地址写进 `sdmc:/switch/nxkit/config.json`。
