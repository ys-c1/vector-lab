#!/bin/bash
# winnorm 端到端验证。
#
#   1) 四组窗口报告：子环数、绕序符号、面积绝对值合计、前后偏差（必须 0）、耗时
#   2) 拒绝用例：自交未拆分、重复点、顶点接触，必须给出序号
#   3) 面积绝对值不变量：TSV 逐项检查偏差恰为 0，外部复算一致
#   4) 关掉新逻辑逐字节回归：render 模块旧路径副本 vs 主驱动 mode=0，
#      并与提交里保存的基线 PGM 逐字节比对
#   另有随机完整性检查（tests/fuzz_arrange.c）：子环互不相交、覆盖不变
#
# 产物在 run-out/（已 gitignore）；基线 PGM 在 baseline/。
set -u
cd "$(dirname "$0")"
OUT=run-out
BASE=baseline
mkdir -p "$OUT"
python3 -I tests/gen_windows.py data >/dev/null || { echo "数据生成失败"; exit 2; }
make -s build/run build/legacy build/test build/fuzz || exit 2
RUN=./build/run
LEGACY=./build/legacy

fail=0
hr() { printf '%s\n' "==================== $* ===================="; }

hr "1) 四组窗口报告（子环数 / 绕序符号 / 面积绝对值 / 偏差 / 耗时）"
$RUN report data/wins_four.txt "$OUT/four.tsv" || fail=1
echo
echo "逐窗口明细（各文件单独跑一遍，确认与合并跑结果相同）："
for f in win_clean_ccw win_clean_cw win_bowtie1 win_double1; do
  printf '[%s]\n' "$f"
  $RUN report "data/$f.txt" | grep -v '^合计' || fail=1
done

hr "2) 拒绝用例（必须给出序号）"
for f in win_bowtie0 win_double0 win_dup win_contact; do
  printf -- '--- %s ---\n' "$f"
  full=$($RUN report "data/$f.txt"); rc=$?
  out=$(printf '%s\n' "$full" | grep -v '^窗口' | grep -v '^合计')
  echo "$out"
  if [ "$rc" -ne 1 ]; then
    echo "FAIL $f 期望退出码 1（拒绝），实际 $rc"; fail=1
  fi
  if ! printf '%s\n' "$out" | grep -q "序号"; then
    echo "FAIL $f 拒绝信息未指出序号"; fail=1
  fi
done

hr "3) 面积绝对值不变量与几何不变（逐位，必须 0 偏差）"
$RUN report data/wins_four.txt "$OUT/four.tsv" >/dev/null || fail=1
python3 -I - "$OUT/four.tsv" <<'PYEOF' || fail=1
import sys
bad = 0
for line in open(sys.argv[1]):
    f = line.rstrip("\n").split("\t")
    if f[0] == "window_id" or f[0] == "-" or not f[0].isdigit():
        continue
    if f[9] != "0":
        print("FAIL 窗口 %s 归一化失败 rc=%s" % (f[0], f[9])); bad = 1; continue
    dev = f[6]
    if dev not in ("0", "0.0", "0.000"):
        print("FAIL 窗口 %s 面积偏差 %s 非零" % (f[0], dev)); bad = 1
    ext = f[7]
    if ext not in ("0", "0.0", "0.000"):
        print("FAIL 窗口 %s 外部复算偏差 %s 非零" % (f[0], ext)); bad = 1
    if f[10] != "0":
        print("FAIL 窗口 %s 归一化前后几何覆盖不一致（%s 个采样点）" % (f[0], f[10])); bad = 1
    signs = f[3].split()
    if len(signs) != int(f[2]):
        print("FAIL 窗口 %s 子环数与符号列长度不符" % f[0]); bad = 1
    if int(f[4]) > int(f[2]):
        print("FAIL 窗口 %s 翻转数超过子环数" % f[0]); bad = 1
    if int(f[2]) < 1:
        print("FAIL 窗口 %s 子环数不为正" % f[0]); bad = 1
print("面积绝对值不变量与几何不变：%s" % ("通过" if bad == 0 else "失败"))
sys.exit(1 if bad else 0)
PYEOF

hr "4) 关掉新逻辑：逐字节回归"
W=304; H=48; SS=4
$LEGACY data/wins_four.txt "$OUT/legacy_render.pgm" $W $H $SS || fail=1
$RUN render data/wins_four.txt $W $H $SS "$OUT/legacy.pgm" "$OUT/normalized.pgm" || fail=1
if [ -f "$BASE/legacy.pgm" ]; then
  ../tools/diff.sh "$OUT/legacy.pgm" "$BASE/legacy.pgm" || fail=1
  ../tools/diff.sh "$OUT/legacy_render.pgm" "$BASE/legacy.pgm" || fail=1
  ../tools/diff.sh "$OUT/normalized.pgm" "$BASE/normalized.pgm" || fail=1
else
  echo "MISSING baseline/legacy.pgm"; fail=1
fi
# 老路径必须与新逻辑逐像素一致（这组窗口归一化前后覆盖相同）
if ! cmp -s "$OUT/legacy.pgm" "$OUT/normalized.pgm"; then
  echo "FAIL 新路径与老路径图不一致"
  fail=1
else
  echo "新路径与老路径逐字节相同（窗口覆盖不变）"
fi

hr "5) 随机完整性（子环互不相交 / 覆盖不变 / 面积逐位不变量）"
./build/fuzz 20000 || fail=1

hr "汇总"
if [ "$fail" -eq 0 ]; then
  echo "ALL CHECKS PASSED"
else
  echo "CHECKS FAILED"
fi
exit "$fail"
