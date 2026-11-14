#!/usr/bin/env bash
# 一键发布容器版本：改版本号 → 构建 NRO → 上传后台 → 校验 sha256 → 提交并打 tag。
#
# 用法：
#   NXKIT_ADMIN_TOKEN=... ./scripts/release.sh 1.7.12 "修复 xxx"
#
# 可选环境变量：
#   NXKIT_SERVER   服务器地址，默认 https://ecfun.cc
#   SKIP_BUILD=1   跳过构建，直接上传已有 NRO（调试用）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-}"
NOTES="${2:-}"
SERVER="${NXKIT_SERVER:-https://ecfun.cc}"
TOKEN="${NXKIT_ADMIN_TOKEN:-}"
CMAKE_FILE="$ROOT/client/apps/miniapp/CMakeLists.txt"
NRO="$ROOT/client/build_switch/apps/miniapp/miniapp.nro"

if [[ -z "$VERSION" ]]; then
  echo "用法: NXKIT_ADMIN_TOKEN=... $0 <版本号> [更新说明]" >&2
  exit 1
fi
if [[ -z "$TOKEN" ]]; then
  echo "缺少 NXKIT_ADMIN_TOKEN（从服务器 /root/NXMiniApp/.env 读取，勿写入仓库）" >&2
  exit 1
fi

# 1. 版本号只维护在 CMakeLists.txt 一处，避免容器版本与发布记录不一致。
python3 - "$CMAKE_FILE" "$VERSION" <<'PY'
import re, sys
path, version = sys.argv[1], sys.argv[2]
text = open(path, encoding='utf-8').read()
updated, count = re.subn(r'set\(NXMINIAPP_VERSION "[^"]*"\)',
                         f'set(NXMINIAPP_VERSION "{version}")', text, count=1)
if count != 1:
    raise SystemExit('未找到 NXMINIAPP_VERSION')
open(path, 'w', encoding='utf-8').write(updated)
PY

# 2. 构建 Switch NRO。
if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  ( cd "$ROOT/client"
    export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
    cmake --build build_switch --target miniapp.nro -j 8 )
fi
if [[ ! -f "$NRO" ]]; then
  echo "未找到构建产物：$NRO" >&2
  exit 1
fi

LOCAL_SHA="$(shasum -a 256 "$NRO" | awk '{print $1}')"
echo "本地 NRO: $NRO"
echo "本地 SHA-256: $LOCAL_SHA"

# 3. 上传发布（同版本号会覆盖更新，接口幂等）。
RESP="$(curl -fsS -X POST "$SERVER/api/v1/admin/releases" \
  -H "Authorization: Bearer $TOKEN" \
  -F "file=@$NRO" \
  -F "version=$VERSION" \
  -F "notes=$NOTES" \
  -F "published=true")"
REMOTE_SHA="$(python3 -c 'import json,sys; print((json.load(sys.stdin) or {}).get("sha256",""))' <<<"$RESP")"

# 4. 校验服务器记录的哈希与本地一致，避免传错文件。
if [[ "$REMOTE_SHA" != "$LOCAL_SHA" ]]; then
  echo "sha256 不一致：本地 $LOCAL_SHA，服务器 ${REMOTE_SHA:-<空>}" >&2
  exit 1
fi
echo "发布成功：v$VERSION（sha256 校验通过）"

# 5. 提交版本号并打 tag（不自动 push，避免误推）。
( cd "$ROOT"
  git add "$CMAKE_FILE"
  git commit -m "chore(release): v$VERSION"
  git tag -a "v$VERSION" -m "v$VERSION${NOTES:+ — $NOTES}" )
echo "已创建提交与 tag v$VERSION，检查无误后执行：git push --follow-tags"
