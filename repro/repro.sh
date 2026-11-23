#!/bin/bash
# 可复现构建：同一组源码构建两次，二进制逐字节相同；两次构建各渲同一场景，图像逐像素一致。
#
# 固定的不确定来源（对应参数）：
#   1. 时间戳   SOURCE_DATE_EPOCH（缺省 0 = 1970-01-01T00:00:00Z）
#               -> __DATE__/__TIME__（-Wdate-time 实测）、tar 成员 mtime、ar 成员时间戳；
#   2. 文件顺序 源码清单显式给出并 sort，链接顺序固定；tar --sort=name；不依赖 shell glob / make wildcard；
#   3. 归档元数据 tar --mtime/--owner=0/--group=0/--numeric-owner/--mode/--format=gnu；
#                 ar rcsD（成员 mtime 归零、owner/group/mode 归一）；
#   4. 环境     LC_ALL=C、TZ、umask、-frandom-seed、-ffile-prefix-map（-g 调试信息里不留源码路径）、
#               -fno-ident、固定 --build-id=sha1。
#
# 两次构建的差异（为了让上面的固定项真正被考验）：
#   a：仓库内快照 snapshot-a，umask 0022，TZ=UTC，源码按正序拷贝、mtime=epoch；
#   b：仓库内快照 snapshot-b，umask 0077，TZ=America/New_York，源码倒序拷贝、mtime=epoch+随机偏移，
#      且整段构建的 cwd / TMPDIR 都不同。
#   两边的源码路径经 -ffile-prefix-map 统一映射到 /src，所以 -g 调试信息也一致。
#
# 用法：repro/repro.sh
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCENE_DIR=$ROOT/repro/scene
OUTROOT=$ROOT/repro/out
BUILDROOT=$ROOT/repro/build

export LC_ALL=C
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}
export ZERO_AR_DATE=1                      # ar：成员时间戳写 0
umask 0022

CC=${CC:-gcc}
BASE_COMMON=(
  -O2 -g -std=c11 -Wall -Wextra
  -fno-ident
  -frandom-seed=vector-lab
  -Wdate-time                            # 源码里若用 __DATE__/__TIME__ 会告警，提醒用 SOURCE_DATE_EPOCH
)
LINK=( -Wl,--build-id=sha1 -lm )

die() { echo "ERROR: $*" >&2; exit 1; }

# ---------- 显式源码清单（含头文件；按路径 sort 后固定） ----------
declare -A SRC HDR INCLUDES EXE
SRC[render]="render/src/export.c render/src/pipeline.c render/src/raster.c render/src/scene.c render/cli.c"
HDR[render]="render/src/export.h render/src/pipeline.h render/src/raster.h render/src/scene.h"
SRC[svg]="svg/src/svgpath.c svg/demo.c"
HDR[svg]="svg/src/svgpath.h"
SRC[gold]="gold/ref.c"
HDR[gold]=""
INCLUDES[render]="-I@SNAP@ -I@SNAP@/render"
INCLUDES[svg]="-I@SNAP@/svg/src"
INCLUDES[gold]=""
EXE[render]=cli
EXE[svg]=cli
EXE[gold]=ref
TAGS=(render svg gold)
for t in "${TAGS[@]}"; do
  SRC[$t]=$(printf '%s\n' ${SRC[$t]} | sort | tr '\n' ' ')
  HDR[$t]=$(printf '%s\n' ${HDR[$t]} | sort | tr '\n' ' ')
done

# ---------- 快照：源码拷贝，mtime 归一 ----------
# snapshot <a|b>
snapshot() {
  local build=$1 snap=$BUILDROOT/snapshot-$1
  local order=normal mtime=$SOURCE_DATE_EPOCH
  [ "$build" = b ] && order=reverse   # b 用相反的文件创建顺序
  rm -rf "$snap"; mkdir -p "$snap"
  local files=()
  local t f
  for t in "${TAGS[@]}"; do
    for f in ${SRC[$t]} ${HDR[$t]}; do files+=("$f"); done
  done
  if [ "$order" = reverse ]; then
    local rev=() i
    for ((i=${#files[@]}-1; i>=0; i--)); do rev+=("${files[i]}"); done
    files=("${rev[@]}")
  fi
  for f in "${files[@]}"; do
    mkdir -p "$snap/$(dirname "$f")"
    cp "$ROOT/$f" "$snap/$f"
  done
  if [ "$build" = b ]; then
    # 给每个文件一个不同的、仍可复现的 mtime（epoch+文件序号），考验“不靠 mtime”
    local i=0
    for f in "${files[@]}"; do
      touch -h -d "@$((SOURCE_DATE_EPOCH + 1000 + i))" "$snap/$f"; i=$((i+1))
    done
  else
    find "$snap" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
  fi
  echo "$snap"
}

# ---------- 编译一组源码 -> 可执行文件 ----------
build_group() {  # build_group <tag> <build> <snap>
  local tag=$1 build=$2 snap=$3
  local bdir=$OUTROOT/$tag/$build
  local objdir=$bdir/obj log=$bdir/logs/compile.log
  mkdir -p "$bdir/bin" "$bdir/logs" "$bdir/artifacts"
  rm -rf "$objdir"; mkdir -p "$objdir"
  : > "$log"
  local inc=${INCLUDES[$tag]//@SNAP@/$snap}
  local common=("${BASE_COMMON[@]}"
                "-ffile-prefix-map=$snap=/src"
                "-fmacro-prefix-map=$snap=/src"
                "-fdebug-prefix-map=$snap=/src")
  local objs=() rel o
  for rel in ${SRC[$tag]}; do
    o=$objdir/$(printf '%s' "$rel" | tr '/' '_').o
    echo "$CC -c $rel" >> "$log"
    ( cd "$snap" && "$CC" "${common[@]}" $inc -c "$rel" -o "$o" ) >> "$log" 2>&1 \
      || die "$tag/$build compile $rel"
    objs+=("$o")
  done
  local out=$bdir/bin/${EXE[$tag]}
  echo "$CC <${#objs[@]} objects> -o bin/${EXE[$tag]}" >> "$log"
  "$CC" "${objs[@]}" "${LINK[@]}" -o "$out" >> "$log" 2>&1 \
    || die "$tag/$build link"
}

# ---------- 确定性归档 ----------
archive() {  # archive <build>
  local build=$1 dest=$OUTROOT/$build.tar.gz
  local stage=$BUILDROOT/archive-stage-$build
  rm -rf "$stage"; mkdir -p "$stage"
  local t
  for t in "${TAGS[@]}"; do
    mkdir -p "$stage/$t/bin"
    cp "$OUTROOT/$t/$build/bin/${EXE[$t]}" "$stage/$t/bin/"
  done
  # 归档前把暂存区 mtime 归一到固定时钟（b 的文件内容与 a 相同，mtime 也强制相同）
  find "$stage" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
  tar --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 --numeric-owner \
      --sort=name --format=gnu --mode='u+rwX,go+rX,go-w' \
      -czf "$dest" -C "$stage" .
}

# ---------- 渲染同一场景 ----------
render_group() {  # render_group <tag> <build>
  local tag=$1 build=$2
  local bdir=$OUTROOT/$tag/$build
  local img=$bdir/artifacts/star.pgm
  case $tag in
    render)
      # scale 0.5：窗口仍取原场景 bbox，几何缩小到左下，图像因此非全白，像素比对才有信息量
      "$bdir/bin/cli" "$SCENE_DIR/star.scene" "$img" 200 120 0.5 \
        > "$bdir/logs/render.log" 2>&1 ;;
    svg)
      "$bdir/bin/cli" "$SCENE_DIR/star.svg" "$img" 200 120 \
        > "$bdir/logs/render.log" 2>&1 ;;
    gold)
      "$bdir/bin/ref" pixels "$SCENE_DIR/star.poly" 200 120 \
        > "$img" 2> "$bdir/logs/render.log" ;;
  esac || die "$tag/$build render"
}

elapsed_ms() { echo $(( ($(date +%s%N) - $1) / 1000000 )); }

# ================= 主流程 =================
rm -rf "$OUTROOT" "$BUILDROOT"; mkdir -p "$OUTROOT" "$BUILDROOT"

declare -A BUILD_MS RENDER_MS
for build in a b; do
  snap=$(snapshot "$build")
  if [ "$build" = b ]; then
    # b 在敌意环境下构建：不同 cwd、TMPDIR、locale 相关变量以外的 TZ、umask
    ( cd /tmp && export TMPDIR=/var/tmp && export TZ=America/New_York && umask 0077 &&
      for t in "${TAGS[@]}"; do
        t0=$(date +%s%N); build_group "$t" "$build" "$snap"; echo "BUILD_MS $t $(elapsed_ms "$t0")"
        t0=$(date +%s%N); render_group "$t" "$build";  echo "RENDER_MS $t $(elapsed_ms "$t0")"
      done ) > "$OUTROOT/.times-$build"
  else
    ( export TZ=UTC && umask 0022 &&
      for t in "${TAGS[@]}"; do
        t0=$(date +%s%N); build_group "$t" "$build" "$snap"; echo "BUILD_MS $t $(elapsed_ms "$t0")"
        t0=$(date +%s%N); render_group "$t" "$build";  echo "RENDER_MS $t $(elapsed_ms "$t0")"
      done ) > "$OUTROOT/.times-$build"
  fi
  archive "$build"
done

while read -r kind t ms; do
  case $kind in
    BUILD_MS)  BUILD_MS[$t,a]=$ms ;;
    RENDER_MS) RENDER_MS[$t,a]=$ms ;;
  esac
done < "$OUTROOT/.times-a"
while read -r kind t ms; do
  case $kind in
    BUILD_MS)  BUILD_MS[$t,b]=$ms ;;
    RENDER_MS) RENDER_MS[$t,b]=$ms ;;
  esac
done < "$OUTROOT/.times-b"
rm -f "$OUTROOT/.times-a" "$OUTROOT/.times-b"

echo "SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH  CC=$($CC --version | head -1)"
echo
echo "== 二进制 =="
printf '%-7s %-18s %-18s %-10s %-8s %-8s\n' 组 sha256_a sha256_b 哈希一致 构建ms_a 构建ms_b
fail=0
for t in "${TAGS[@]}"; do
  a=$OUTROOT/$t/a/bin/${EXE[$t]}; b=$OUTROOT/$t/b/bin/${EXE[$t]}
  ha=$(sha256sum "$a" | cut -d' ' -f1); hb=$(sha256sum "$b" | cut -d' ' -f1)
  same=yes; [ "$ha" = "$hb" ] || { same=NO; fail=1; }
  printf '%-7s %-18s %-18s %-10s %-8s %-8s\n' "$t" "$ha" "$hb" "$same" "${BUILD_MS[$t,a]}" "${BUILD_MS[$t,b]}"
done

echo
echo "== 渲染（同场景 200x120；a/b 两次构建各渲一遍） =="
printf '%-7s %-6s %-8s %-8s %-8s %-9s %-8s\n' 组 图像 字节_a 字节_b 差异像素 头一致 渲染ms_a/b
for t in "${TAGS[@]}"; do
  out=$(python3 -I - "$OUTROOT/$t/a/artifacts/star.pgm" "$OUTROOT/$t/b/artifacts/star.pgm" <<'PY'
import sys
def load(p):
    d = open(p, 'rb').read()
    assert d[:2] == b'P5', p
    i, fields = 2, []
    while len(fields) < 3:
        while d[i:i+1] in b' \t\r\n': i += 1
        if d[i:i+1] == b'#':
            i = d.index(b'\n', i) + 1; continue
        j = i
        while d[j:j+1] not in b' \t\r\n': j += 1
        fields.append(d[i:j]); i = j
    i += 1
    return b' '.join(fields), d[i:]
ha, pa = load(sys.argv[1]); hb, pb = load(sys.argv[2])
diff = sum(1 for x, y in zip(pa, pb) if x != y) + abs(len(pa) - len(pb))
on = sum(1 for v in pa if v)
print(len(pa), len(pb), diff, 'yes' if ha == hb else 'no', on)
sys.exit(0 if diff == 0 and ha == hb else 1)
PY
)
  rc=$?
  read -r bytes_a bytes_b diff hdr on <<<"$out"
  [ $rc -eq 0 ] || fail=1
  printf '%-7s %-6s %-8s %-8s %-8s %-9s %s/%s  (on=%s)\n' "$t" P5 \
    "$bytes_a" "$bytes_b" "$diff" "$hdr" "${RENDER_MS[$t,a]}" "${RENDER_MS[$t,b]}" "$on"
done

echo
echo "== 归档（确定性 tar.gz） =="
for build in a b; do
  printf '%-3s %s  %s  %s\n' "$build" \
    "$(sha256sum "$OUTROOT/$build.tar.gz" | cut -d' ' -f1)" \
    "$(stat -c %s "$OUTROOT/$build.tar.gz")B" \
    "成员: $(tar -tzf "$OUTROOT/$build.tar.gz" | sort | tr '\n' ' ')"
  printf '    元数据样例: %s\n' "$(tar -tvzf "$OUTROOT/$build.tar.gz" | sed -n '2p')"
done
[ "$(sha256sum "$OUTROOT/a.tar.gz" | cut -d' ' -f1)" = "$(sha256sum "$OUTROOT/b.tar.gz" | cut -d' ' -f1)" ] || fail=1

echo
if [ $fail -eq 0 ]; then echo "RESULT: REPRODUCIBLE (二进制哈希一致 / 图像差异像素 0 / 归档哈希一致)";
else echo "RESULT: NOT REPRODUCIBLE"; exit 1; fi
