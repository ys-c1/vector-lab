#!/bin/bash
# 浮点确定性验证：
#   1) 四组场景各用确定性配置(fp-on)渲染两次，逐字节比对，报告差异字节/耗时/配置快照；
#      两次结果不同即判失败。
#   2) 关闭确定性(fp-off / 缺省)时，新二进制产物必须与改动前基线二进制逐字节一致。
# 用法: render/det_check.sh
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT=$ROOT/artifacts/fpdet
BASE_BIN=$ROOT/artifacts/baseline/cli-head
CONF_ON=$HERE/configs/fp-on.conf
CONF_OFF=$HERE/configs/fp-off.conf
W=256
H=256

# 场景: 文件名  缩放  旋转角(弧度, 0 表示同时参与基线回落比对)
SCENES=(
  "s1_square.txt            1.0   0.0"
  "s2_rotated_square.txt    1.0   0.5235987755982988"
  "s3_fractional_triangle.txt 1.0 0.19739555984988078"
  "s4_multi_poly.txt        1.0   0.7853981633974483"
)

mkdir -p "$OUT"
echo "== build =="
make -C "$HERE" clis >/dev/null || { echo "build failed"; exit 2; }
CLI_BASE=$HERE/build/cli        # 基线标志构建
CLI_DET=$HERE/build/cli-det     # 确定性标志构建(-ffp-contract=off)
# 改动前基线二进制：缺失则从基线提交可复现地重建（见 build_baseline.sh）。
if [ ! -x "$BASE_BIN" ]; then
  "$HERE/build_baseline.sh" || { echo "baseline build failed"; exit 2; }
fi

now_ms() { date +%s%N; }
elapsed_ms() { echo "scale=3; ($2 - $1) / 1000000" | bc; }
diff_bytes() { if cmp -s "$1" "$2"; then echo 0; else cmp -l "$1" "$2" | wc -l | tr -d ' '; fi; }
snapshot_of() { sed -n 's/^#.*$//; /./p' "$1" | tr '\n' ' ' | sed 's/ $//'; }

REPORT=$OUT/report.txt
: > "$REPORT"
FAIL=0
SNAP_ON=$(snapshot_of "$CONF_ON")
SNAP_OFF=$(snapshot_of "$CONF_OFF")

emit() { echo "$@" | tee -a "$REPORT"; }

emit "浮点确定性报告  $(date -u +%Y-%m-%dT%H:%M:%SZ)"
emit "grid=${W}x${H}  supersample=4"
emit "确定性构建标志: -O2 -std=c11 -ffp-contract=off -fno-fast-math"
emit "基线构建标志  : -O2 -std=c11 (历史默认)"
emit ""
emit "== A. 确定性双跑逐字节比对 (配置: $SNAP_ON) =="
printf "%-26s %10s %10s %10s %s\n" "scene" "run1(ms)" "run2(ms)" "diff字节" "判定" | tee -a "$REPORT"

for entry in "${SCENES[@]}"; do
  # shellcheck disable=SC2086
  set -- $entry
  scene=$1; sc=$2; rot=$3
  src=$HERE/scenes/$scene
  o1=$OUT/${scene%.txt}.on.1.pgm
  o2=$OUT/${scene%.txt}.on.2.pgm
  t0=$(now_ms); "$CLI_DET" "$src" "$o1" "$W" "$H" "$sc" --rot "$rot" --conf "$CONF_ON" >/dev/null; t1=$(now_ms)
  t2=$(now_ms); "$CLI_DET" "$src" "$o2" "$W" "$H" "$sc" --rot "$rot" --conf "$CONF_ON" >/dev/null; t3=$(now_ms)
  d=$(diff_bytes "$o1" "$o2")
  e1=$(elapsed_ms "$t0" "$t1"); e2=$(elapsed_ms "$t2" "$t3")
  if [ "$d" = "0" ]; then verdict="PASS"; else verdict="FAIL"; FAIL=$((FAIL+1)); fi
  printf "%-26s %10s %10s %10s %s\n" "$scene" "$e1" "$e2" "$d" "$verdict" | tee -a "$REPORT"
done

emit ""
emit "== B. 关闭确定性回落基线，逐字节比对 (rot=0) =="
emit "比对三方: 改动前基线二进制 / 新CLI(基线标志,无conf) / 新CLI(确定性标志,conf=fp-off)"
printf "%-26s %12s %12s %s\n" "scene" "base-vs-new" "new-vs-off" "判定" | tee -a "$REPORT"

for entry in "${SCENES[@]}"; do
  # shellcheck disable=SC2086
  set -- $entry
  scene=$1; sc=$2
  src=$HERE/scenes/$scene
  ob=$OUT/${scene%.txt}.base.pgm     # 改动前基线二进制(rot 恒为 0)
  on=$OUT/${scene%.txt}.new.pgm      # 新 CLI 基线标志、无 conf(rot=0)
  oo=$OUT/${scene%.txt}.off.pgm      # 新 CLI 确定性标志、fp-off(rot=0)
  "$BASE_BIN" "$src" "$ob" "$W" "$H" "$sc" >/dev/null
  "$CLI_BASE" "$src" "$on" "$W" "$H" "$sc" >/dev/null
  "$CLI_DET"  "$src" "$oo" "$W" "$H" "$sc" --conf "$CONF_OFF" >/dev/null
  d1=$(diff_bytes "$ob" "$on")
  d2=$(diff_bytes "$on" "$oo")
  if [ "$d1" = "0" ] && [ "$d2" = "0" ]; then verdict="PASS"; else verdict="FAIL"; FAIL=$((FAIL+1)); fi
  printf "%-26s %12s %12s %s\n" "$scene" "$d1" "$d2" "$verdict" | tee -a "$REPORT"
done

emit ""
emit "== C. 配置项快照 =="
emit "fp-on : $SNAP_ON"
emit "fp-off: $SNAP_OFF"
emit ""
if [ "$FAIL" = "0" ]; then emit "总判定: PASS (全部逐字节一致)"; else emit "总判定: FAIL ($FAIL 项不一致)"; fi
echo "report: $REPORT"
exit "$FAIL"
