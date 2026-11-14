#!/usr/bin/env bash
# 从代码生成能力清单，防止文档与实现漂移：
#   - 支持的 WXML 标签取自 miniapp_view.cpp 的 kKnownTags
#   - 公开 API 取自 internal/api/router.go 的 v1 路由注册
# 生成结果写入 miniapp-capabilities.md 的 BEGIN/END 标记之间。
#
# 用法：./scripts/gen-capabilities.sh [--check]
#   --check  只校验文档是否最新（CI 可用），不修改文件
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPP="$ROOT/client/apps/miniapp/source/miniapp_view.cpp"
ROUTER="$ROOT/server/internal/api/router.go"
DOC="$ROOT/skill/nxminiapp-ops/references/miniapp-capabilities.md"
CHECK=0
[[ "${1:-}" == "--check" ]] && CHECK=1

GENERATED="$(python3 - "$CPP" "$ROUTER" <<'PY'
import re, sys

cpp = open(sys.argv[1], encoding='utf-8').read()
router = open(sys.argv[2], encoding='utf-8').read()

match = re.search(r'kKnownTags\[\]\s*=\s*\{(.*?)\};', cpp, re.S)
if not match:
    raise SystemExit('未找到 kKnownTags')
tags = re.findall(r'"([a-z-]+)"', match.group(1))

routes = re.findall(r'v1\.(GET|POST|PUT|PATCH|DELETE)\("([^"]+)"', router)

lines = ['<!-- 本段由 scripts/gen-capabilities.sh 自动生成，请勿手改 -->',
         '',
         '### 支持的 WXML 标签',
         '',
         '`' + '`, `'.join(sorted(set(tags))) + '`',
         '',
         '### 公开 API（/api/v1）',
         '',
         '| 方法 | 路径 |',
         '|---|---|']
for method, path in routes:
    lines.append(f'| {method} | `/api/v1{path}` |')
lines.append('')
print('\n'.join(lines))
PY
)"

python3 - "$DOC" "$GENERATED" "$CHECK" <<'PY'
import sys

path, generated, check = sys.argv[1], sys.argv[2], sys.argv[3] == '1'
text = open(path, encoding='utf-8').read()
begin = '<!-- BEGIN:generated -->'
end = '<!-- END:generated -->'
if begin not in text or end not in text:
    raise SystemExit('文档缺少 BEGIN/END 标记')
head, rest = text.split(begin, 1)
_, tail = rest.split(end, 1)
updated = head + begin + '\n' + generated + '\n' + end + tail
if check:
    if updated != text:
        print('能力清单已过期，请运行 scripts/gen-capabilities.sh', file=sys.stderr)
        sys.exit(1)
    print('能力清单已是最新')
    sys.exit(0)
open(path, 'w', encoding='utf-8').write(updated)
print('已更新', path)
PY
