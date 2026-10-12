#!/usr/bin/env bash
#
# 生成 compile_commands.json，供 clangd / VSCode C++ 插件读取编译参数。
#
# 为什么需要它：本项目的构建入口是手写 Makefile，IDE 读不到它的 -I / -D / -std，
# 于是把 src/main.cpp 报成「'httplib.h' file not found」等一堆假错误。
# 试过用 .clangd 写相对路径 -Isrc，但 clangd 会把编译目录设成源文件所在目录，
# 于是 -Isrc 解析成 src/src 仍然找不到。必须用带绝对路径的 compilation database。
#
# 本文件是构建产物（内含本机绝对路径），已加入 .gitignore。
#
# 用法（一般不用直接调用，交给 make compdb）：
#   tools/gen_compdb.sh <项目根> <编译命令前缀> <目标文件目录> <源文件>...

set -euo pipefail

if [ $# -lt 4 ]; then
  echo "用法: tools/gen_compdb.sh <项目根> <编译命令前缀> <目标文件目录> <源文件>..." >&2
  exit 2
fi

ROOT="$1"
CMD_PREFIX="$2"
OBJ_DIR="$3"
shift 3

OUT="${ROOT}/compile_commands.json"

{
  printf '[\n'
  first=1
  for src in "$@"; do
    [ "$first" -eq 1 ] || printf ',\n'
    first=0
    printf '  {"directory": "%s", "file": "%s", "command": "%s -c %s -o %s/%s.o"}' \
      "$ROOT" "$src" "$CMD_PREFIX" "$src" "$OBJ_DIR" "$src"
  done
  printf '\n]\n'
} > "$OUT"

# 自查：路径里若混进引号或反斜杠，JSON 会坏掉，而 IDE 只会静默失效
if printf '%s' "$ROOT$CMD_PREFIX$OBJ_DIR" | grep -qE '["\\]'; then
  echo "[FAIL] 路径或编译参数中含引号/反斜杠，无法安全写入 JSON：$OUT" >&2
  exit 3
fi

count=$#
if command -v python3 >/dev/null 2>&1; then
  if ! python3 -I -c "import json,sys; json.load(open(sys.argv[1]))" "$OUT"; then
    echo "[FAIL] 生成的 compile_commands.json 不是合法 JSON：$OUT" >&2
    exit 4
  fi
fi

echo "[OK] 已生成 compile_commands.json（${count} 条编译命令）"
