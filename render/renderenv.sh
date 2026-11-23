#!/bin/sh
# 四环境确定性验证。
#
# 同一份二进制、同一份场景、同一条命令行，只在四组环境变量下各跑两次：
#   a-path   工作目录：带空格+中文的深层目录  vs  纯 ASCII 的一级目录
#   b-time   时区+挂钟：UTC+14/时刻A  vs  UTC-11/时刻B（LD_PRELOAD 冻钟）
#   c-locale 语言：de_DE（逗号小数点）vs C.UTF-8；两者都模拟“宿主已采用环境 locale”
#   d-umask  掩码：umask 077  vs  umask 027
# 判据：
#   1. 每个环境内部两次渲染逐像素一致；
#   2. 四组共 8 张图两两一致；
#   3. 全部与 render/baseline/scene.pgm 一致；
#   4. 文件权限四组都是 644、字节与 SHA-256 相同。
# 任一不满足 -> 退出码 1。缺前置产物 -> 退出码 2。
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)
OUT=${OUT:-$ROOT/artifacts/determinism}
CLI=${CLI:-$HERE/build/cli}
DIFF=${DIFF:-$HERE/build/pgmdiff}
BASE=$HERE/baseline/scene.pgm
SCENE=$OUT/input/scene.txt
W=160; H=120; SCALE=1.25
EPOCH_A=1700000000
EPOCH_B=2000000000

say() { printf '%s\n' "$*"; }
fail=0

for need in "$CLI" "$DIFF" "$BASE" "$SCENE" \
            "$OUT/build/faketick.so" "$OUT/build/adoptlocale.so" "$OUT/build/localeprobe"; do
  [ -e "$need" ] || { say "FAIL missing $need"; say "  hint: make -C render build/cli build/pgmdiff && ./render/mkenv.sh"; exit 2; }
done

# LD_PRELOAD 的值按空白切分，库路径含空格/中文会静默不加载。挂到无空白的
# 固定路径（优先 /dev/shm），并让下面的探针验证确实生效。
stage_shim() { # $1 源 .so  $2 短名 -> 打印可用的绝对路径
  src=$1; short=$2
  case "$src" in
    *[!A-Za-z0-9/._-]*)
      if [ -d /dev/shm ] && cp "$src" "/dev/shm/$short.$$.so" 2>/dev/null; then
        printf '%s\n' "/dev/shm/$short.$$.so"
      else
        cp "$src" "./$short.$$.so" || return 1
        printf '%s\n' "$(pwd)/$short.$$.so"
      fi
      ;;
    *) printf '%s\n' "$src" ;;
  esac
}

SHIM_TIME=$(stage_shim "$OUT/build/faketick.so" faketick) || { say "FAIL cannot stage faketick.so"; exit 2; }
SHIM_LOC=$(stage_shim "$OUT/build/adoptlocale.so" adoptlocale) || { say "FAIL cannot stage adoptlocale.so"; exit 2; }
trap 'rm -f "$SHIM_TIME" "$SHIM_LOC"' EXIT INT TERM

# 前置自证：两个 shim 必须真的生效，否则后面的“一致”毫无意义。
probe_t=$(env -i PATH=/usr/bin:/bin FAKETICK_EPOCH=$EPOCH_A LD_PRELOAD="$SHIM_TIME" \
          "$OUT/build/tickprobe" 2>/dev/null | sed -n 's/^time=//p')
[ "$probe_t" = "$EPOCH_A" ] || { say "FAIL clock shim inactive (time=$probe_t)"; exit 2; }
probe_l=$(env -i PATH=/usr/bin:/bin LANG=de_DE.UTF-8 LC_ALL=de_DE.UTF-8 \
          LOCPATH="$OUT/envs/c-locale/loc" LD_PRELOAD="$SHIM_LOC" \
          "$OUT/build/localeprobe" 2>/dev/null)
[ "$probe_l" = "decimal_point=," ] || { say "FAIL locale shim inactive ($probe_l)"; exit 2; }

RC1=0; RC2=0
run_pair() { # $1 环境名；每个环境两个变体都跑同一条 CLI 命令行
  name=$1
  d="$OUT/runs/$name"
  mkdir -p "$d"
  rm -f "$d/run1.pgm" "$d/run2.pgm"
  rc1=0; rc2=0
  case "$name" in
    a-path)
      # 变体 1：带空格+中文的深层目录，输入输出都用相对路径
      w1="$OUT/envs/a-path/work/场景 目录/sub dir"
      mkdir -p "$w1"
      cp "$SCENE" "$w1/scene.txt"
      ( cd "$w1" && env -i PATH=/usr/bin:/bin HOME="$OUT/envs/a-path/home" \
          TMPDIR="$OUT/envs/a-path/tmp" LANG=C LC_ALL=C \
          "$CLI" scene.txt run1.pgm "$W" "$H" "$SCALE" ) >"$d/run1.log" 2>&1
      rc1=$?
      [ -f "$w1/run1.pgm" ] && mv "$w1/run1.pgm" "$d/run1.pgm"
      # 变体 2：纯 ASCII 的一级目录，绝对路径调用
      w2="$OUT/envs/a-path/plain"
      mkdir -p "$w2"
      ( cd "$w2" && env -i PATH=/usr/bin:/bin LANG=C LC_ALL=C \
          "$CLI" "$SCENE" "$d/run2.pgm" "$W" "$H" "$SCALE" ) >"$d/run2.log" 2>&1
      rc2=$?
      ;;
    b-time)
      # 变体 1：UTC+14，钟停在 EPOCH_A；变体 2：UTC-11，钟停在 EPOCH_B
      env -i PATH=/usr/bin:/bin TZ=Pacific/Kiritimati FAKETICK_EPOCH=$EPOCH_A \
          LD_PRELOAD="$SHIM_TIME" \
          "$CLI" "$SCENE" "$d/run1.pgm" "$W" "$H" "$SCALE" >"$d/run1.log" 2>&1
      rc1=$?
      env -i PATH=/usr/bin:/bin TZ=Pacific/Pago_Pago FAKETICK_EPOCH=$EPOCH_B \
          LD_PRELOAD="$SHIM_TIME" \
          "$CLI" "$SCENE" "$d/run2.pgm" "$W" "$H" "$SCALE" >"$d/run2.log" 2>&1
      rc2=$?
      ;;
    c-locale)
      # 变体 1：de_DE（小数点是逗号）；变体 2：C.UTF-8。
      # 两者都让宿主先 setlocale(LC_ALL, "")，进程启动后不再是 C locale。
      env -i PATH=/usr/bin:/bin LANG=de_DE.UTF-8 LC_ALL=de_DE.UTF-8 \
          LOCPATH="$OUT/envs/c-locale/loc" LD_PRELOAD="$SHIM_LOC" \
          "$CLI" "$SCENE" "$d/run1.pgm" "$W" "$H" "$SCALE" >"$d/run1.log" 2>&1
      rc1=$?
      env -i PATH=/usr/bin:/bin LANG=C.UTF-8 LC_ALL=C.UTF-8 LC_NUMERIC=C.UTF-8 \
          LD_PRELOAD="$SHIM_LOC" \
          "$CLI" "$SCENE" "$d/run2.pgm" "$W" "$H" "$SCALE" >"$d/run2.log" 2>&1
      rc2=$?
      ;;
    d-umask)
      ( umask 077 && env -i PATH=/usr/bin:/bin LANG=C LC_ALL=C \
          "$CLI" "$SCENE" "$d/run1.pgm" "$W" "$H" "$SCALE" ) >"$d/run1.log" 2>&1
      rc1=$?
      ( umask 027 && env -i PATH=/usr/bin:/bin LANG=C LC_ALL=C \
          "$CLI" "$SCENE" "$d/run2.pgm" "$W" "$H" "$SCALE" ) >"$d/run2.log" 2>&1
      rc2=$?
      ;;
  esac
  RC1=$rc1; RC2=$rc2
  [ $rc1 -eq 0 ] && [ $rc2 -eq 0 ]
}

REPORT=$OUT/report.tsv
FILES=$OUT/files.tsv
: > "$REPORT"; : > "$FILES"
printf 'env\tdiff_pixels\tbytes\tmode\tsha256\telapsed_ms\tresult\n' >> "$REPORT"
printf 'env\tvariant\tbytes\tmode\tsha256\n' >> "$FILES"

say "== 四环境：同一场景 $SCENE -> ${W}x${H}, scale=$SCALE，每环境两次 =="
say ""
printf '%-9s %-11s %-8s %-7s %-9s %-64s %s\n' ENV DIFF_PIXELS BYTES MODE ELAPSED_MS SHA256 RESULT
for name in a-path b-time c-locale d-umask; do
  d="$OUT/runs/$name"
  t0=$(date +%s%N)
  run_pair "$name"; rc=$?
  t1=$(date +%s%N)
  ms=$(( (t1 - t0) / 1000000 ))

  if [ $rc -ne 0 ] || [ ! -f "$d/run1.pgm" ] || [ ! -f "$d/run2.pgm" ]; then
    # 把两个变体各自的退出码和产物情况都写出来，失败才可诊断
    v1="rc=$RC1"; v2="rc=$RC2"
    [ -f "$d/run1.pgm" ] || v1="$v1,no-pgm"
    [ -f "$d/run2.pgm" ] || v2="$v2,no-pgm"
    printf '%-9s %-11s %-8s %-7s %-9s %-64s %s\n' "$name" - - - "$ms" - "RENDER_FAILED"
    say "          variant1: $v1   variant2: $v2"
    printf '%s\tNA\tNA\tNA\tNA\t%s\tFAIL(%s;%s)\n' "$name" "$ms" "$v1" "$v2" >> "$REPORT"
    fail=1
    continue
  fi

  intra=$("$DIFF" "$d/run1.pgm" "$d/run2.pgm"); irc=$?
  base=$("$DIFF" "$BASE" "$d/run1.pgm"); brc=$?
  base2=$("$DIFF" "$BASE" "$d/run2.pgm"); brc2=$?
  dpix=$(printf '%s' "$base" | sed -n 's/^diff \([0-9]*\)\/.*/\1/p')
  bytes=$(wc -c < "$d/run1.pgm")
  sha=$(sha256sum "$d/run1.pgm" | cut -d' ' -f1)
  mode1=$(stat -c '%a' "$d/run1.pgm")
  mode2=$(stat -c '%a' "$d/run2.pgm")

  verdict=OK
  [ $irc -eq 0 ] || { verdict="INTRA_DIFF"; fail=1; }
  [ $brc -eq 0 ] && [ $brc2 -eq 0 ] || { verdict="MISMATCH"; fail=1; }
  [ "$mode1" = "644" ] && [ "$mode2" = "644" ] || { verdict="MODE=$mode1/$mode2"; fail=1; }

  printf '%-9s %-11s %-8s %-7s %-9s %-64s %s\n' "$name" "$dpix" "$bytes" "$mode1/$mode2" "$ms" "$sha" "$verdict"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$dpix" "$bytes" "$mode1" "$sha" "$ms" "$verdict" >> "$REPORT"
  [ $irc -ne 0 ] && say "          intra-env: $intra"

  # 该环境产出的全部文件，逐个记录；-exec 传路径，文件名含空格也安全
  find "$d" -type f -exec sh -c '
    dir=$1; name=$2; files=$3; shift 3
    for f in "$@"; do
      rel=${f#"$dir/"}
      printf "%s\t%s\t%s\t%s\t%s\n" "$name" "$rel" "$(wc -c < "$f")" \
        "$(stat -c "%a" "$f")" "$(sha256sum "$f" | cut -d" " -f1)" >> "$files"
    done
  ' sh "$d" "$name" "$FILES" {} + 2>/dev/null
done

# ---- 环境自证：证明每组确实改变了它声称的维度 ----
say ""
say "== 环境自证 =="
PDIR="$OUT/envs/a-path/work/场景 目录/sub dir"
[ -f "$PDIR/scene.txt" ] && say "  a-path   ran in '${PDIR#"$OUT/envs/a-path/"}' and in 'plain/'" \
                         || { say "  a-path   MISSING nested work dir"; fail=1; }

u=$(env -i PATH=/usr/bin:/bin TZ=UTC FAKETICK_EPOCH=$EPOCH_A LD_PRELOAD="$SHIM_TIME" date +%H)
k=$(env -i PATH=/usr/bin:/bin TZ=Pacific/Kiritimati FAKETICK_EPOCH=$EPOCH_A LD_PRELOAD="$SHIM_TIME" date +%H)
p=$(env -i PATH=/usr/bin:/bin TZ=Pacific/Pago_Pago FAKETICK_EPOCH=$EPOCH_B LD_PRELOAD="$SHIM_TIME" date +'%Y-%m-%d %H')
if [ "$u" != "$k" ]; then
  say "  b-time   instant A: UTC=${u}h vs UTC+14=${k}h; instant B (UTC-11): $p"
else
  say "  b-time   TZ had no effect (both ${u}h)"; fail=1
fi

dec=$(env -i PATH=/usr/bin:/bin LANG=de_DE.UTF-8 LC_ALL=de_DE.UTF-8 \
      LOCPATH="$OUT/envs/c-locale/loc" locale -k LC_NUMERIC 2>/dev/null |
      sed -n 's/^decimal_point="\(.*\)"/\1/p')
[ "$dec" = "," ] && say "  c-locale decimal_point=','(variant 1); variant 2 = C.UTF-8" \
                 || { say "  c-locale decimal_point='$dec' (want ',')"; fail=1; }

m7=$( ( umask 077; t="$OUT/runs/d-umask/probe.$$"; : > "$t"; stat -c '%a' "$t"; rm -f "$t" ) )
m2=$( ( umask 027; t="$OUT/runs/d-umask/probe2.$$"; : > "$t"; stat -c '%a' "$t"; rm -f "$t" ) )
say "  d-umask  fresh files under umask 077/027 = $m7/$m2; exported images must be 644"

# ---- 交叉比对：8 张图两两一致 ----
say ""
say "== 交叉比对 =="
LIST="$OUT/runs/.cmp_list.$$"
: > "$LIST"
for name in a-path b-time c-locale d-umask; do
  for v in 1 2; do
    f="$OUT/runs/$name/run$v.pgm"
    [ -f "$f" ] && printf '%s\n' "$f" >> "$LIST"
  done
done
total=$(wc -l < "$LIST" | tr -d ' ')
pairs=0; badpairs=0
i=0
while IFS= read -r a; do
  i=$((i+1)); j=0
  while IFS= read -r b; do
    j=$((j+1))
    [ "$j" -le "$i" ] && continue
    pairs=$((pairs+1))
    r=$("$DIFF" "$a" "$b") || { badpairs=$((badpairs+1)); say "  DIFF ${a#"$OUT/runs/"} vs ${b#"$OUT/runs/"}: $r"; }
  done < "$LIST"
done < "$LIST"
rm -f "$LIST"
if [ "$badpairs" -eq 0 ] && [ "$pairs" -eq 28 ]; then
  say "  $total 张图、$pairs 对全部逐像素一致"
else
  say "  images=$total pairs=$pairs differing=$badpairs"
  fail=1
fi

# ---- 文件清单 ----
say ""
say "== 每个环境产出的文件 =="
printf '%-9s %-16s %-8s %-5s %s\n' ENV FILE BYTES MODE SHA256
{ head -1 "$FILES"; tail -n +2 "$FILES" | sort; } |
while IFS="$(printf '\t')" read -r e v b m hh; do
  [ "$e" = env ] && continue
  printf '%-9s %-16s %-8s %-5s %s\n' "$e" "$v" "$b" "$m" "$hh"
done

say ""
if [ $fail -ne 0 ]; then say "RESULT: FAIL"; exit 1; fi
say "RESULT: PASS"
