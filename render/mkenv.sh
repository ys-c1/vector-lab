#!/bin/sh
# 准备四组隔离环境。所有路径由脚本自身位置推出，不写死机器绝对路径。
# 产物统一落在 "$OUT"（默认 <repo>/artifacts/determinism）。
#   .../input/scene.txt        四个环境共读的同一份场景字节
#   .../envs/a-path/           带空格与中文的深层工作目录
#   .../envs/b-time/           TZ 与 faketick 配置
#   .../envs/c-locale/loc/     localedef 离线生成的 de_DE.UTF-8（逗号小数点）
#   .../envs/d-umask/          umask 077 环境
#   .../build/                 faketick.so、tickprobe（由 render/faketick.c、tickprobe.c 编译）
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)
OUT=${OUT:-$ROOT/artifacts/determinism}
SRC=${SRC:-$HERE}
mkdir -p "$OUT/input" "$OUT/envs" "$OUT/runs" "$OUT/build" "$OUT/logs"

# --- 1. 路径：空格 + 中文 + 两级子目录 ---
P="$OUT/envs/a-path"
mkdir -p "$P/work/场景 目录/sub dir" "$P/home" "$P/tmp"
cp "$ROOT/render/baseline/scene.txt" "$P/work/场景 目录/scene.txt"

# --- 2. 时区：UTC+14（Pacific/Kiritimati），并把挂钟冻住 ---
T="$OUT/envs/b-time"
mkdir -p "$T"
printf 'TZ=Pacific/Kiritimati\nFAKETICK_EPOCH=1700000000\n' > "$T/env"

# --- 3. 语言：de_DE.UTF-8，离线生成，避免依赖宿主已装的 locale ---
L="$OUT/envs/c-locale"
mkdir -p "$L/loc"
if [ ! -f "$L/loc/de_DE.UTF-8/LC_NUMERIC" ]; then
  if ! localedef -i de_DE -f UTF-8 "$L/loc/de_DE.UTF-8" >/dev/null 2>&1; then
    echo "mkenv: localedef de_DE failed (need /usr/share/i18n/locales/de_DE)" >&2
    exit 2
  fi
fi
printf 'LANG=de_DE.UTF-8\nLC_ALL=de_DE.UTF-8\nLOCPATH=%s/loc\n' "$L" > "$L/env"

# --- 4. 掩码：umask 077 ---
M="$OUT/envs/d-umask"
mkdir -p "$M"
printf 'UMASK=077\n' > "$M/env"

cp "$ROOT/render/baseline/scene.txt" "$OUT/input/scene.txt"

# --- 时间 shim：把墙钟钉在 FAKETICK_EPOCH ---
mkdir -p "$OUT/build"
# 编译辅助程序：每个路径都加引号，工作目录含空格/中文也能过。
build_aux() { # $1 输出文件名(相对 build/)  $2 日志名  其余为 cc 参数
  out=$1; log=$2; shift 2
  [ -x "$OUT/build/$out" ] && return 0
  if ! cc -O2 -Wall -Wextra "$@" -o "$OUT/build/$out" 2>"$OUT/logs/$log"; then
    echo "mkenv: $out build failed (see logs/$log)" >&2
    return 1
  fi
}

build_aux faketick.so    faketick.build.log    -shared -fPIC "$SRC/faketick.c" -ldl || exit 2
build_aux adoptlocale.so adoptlocale.build.log -shared -fPIC "$SRC/adoptlocale.c" || exit 2
build_aux tickprobe      tickprobe.build.log   "$SRC/tickprobe.c" || exit 2
build_aux localeprobe    localeprobe.build.log  "$SRC/localeprobe.c" || exit 2

printf '%s\n' "$OUT"
