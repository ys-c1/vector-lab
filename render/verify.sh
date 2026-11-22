#!/bin/bash
# 端到端验证：
#   1) 关掉新逻辑（旧位置参数调用）时，产物与 render/baseline 逐字节相同
#   2) 新预算模式下四档预算的分配表，指标必须随预算单调不升
#   3) 预算为零明确报出；越界分配被拒并说明超了哪一项
set -u
cd "$(dirname "$0")"

CLI=build/cli
BENCH=build/bench
fail=0
say() { printf '%s\n' "$*"; }
bad() { printf 'FAIL %s\n' "$*"; fail=1; }

make -s build/cli build/bench build/goldcheck >/dev/null || { bad "build"; exit 1; }

say "== 0. 新渲染器 vs 独立 gold 参照（8x8 灰阶）=="
GOLD=../gold/build/ref
if [ ! -x "$GOLD" ]; then make -s -C ../gold >/dev/null 2>&1; fi
if [ -x "$GOLD" ]; then
  for pair in "data/jag.txt 61 61" "data/tri.txt 97 55"; do
    set -- $pair
    if build/goldcheck "$1" "$GOLD" 8 "$2" "$3" >/tmp/gc.txt 2>&1; then
      say "  OK $(cat /tmp/gc.txt)"
    else
      bad "goldcheck $pair: $(cat /tmp/gc.txt)"
    fi
  done
else
  say "  SKIP (gold/ref not built)"
fi

say "== 1. 旧链路逐字节不变 =="
while read -r name scene w h sc; do
  case "$name" in \#*|"") continue;; esac
  out=$(mktemp)
  "$CLI" "$scene" "$out" "$w" "$h" "$sc" >/dev/null || { bad "legacy run $name"; rm -f "$out"; continue; }
  if cmp -s "baseline/$name.pgm" "$out"; then
    say "  IDENTICAL $name"
  else
    bad "legacy $name differs from baseline ($(cmp -l "baseline/$name.pgm" "$out" 2>/dev/null | head -1 | awk '{print "byte "$1}'))"
  fi
  rm -f "$out"
done < baseline/manifest.txt

say "== 2. 四档预算的分配与指标曲线 =="
TIERS=0,4096,16384,65536,262144
for scene in data/jag.txt data/tri.txt data/two.txt; do
  say "  -- $scene --"
  "$BENCH" "$scene" "$TIERS" > /tmp/bench.tsv
  rc=$?
  awk -F'\t' 'NR>2 && $1!="0" && $6!="NO_FIT" { printf "    budget %-7s  %sx%s  ss=%s  cost=%-7s  jag=%s  alloc=%sms  render=%sms  mono=%s\n",$1,$2,$3,$4,$5,$6,$7,$8,$9 }' /tmp/bench.tsv
  awk -F'\t' 'NR>2 && $1=="0" { printf "    budget 0        -> %s\n",$6 }' /tmp/bench.tsv
  awk -F'\t' 'NR>2 && $6=="NO_FIT" { printf "    budget %-7s  -> %s\n",$1,$6 }' /tmp/bench.tsv
  [ $rc -eq 0 ] || bad "monotone violated for $scene"
done

say "== 3. 预算为零与越界拒绝 =="
out=$(mktemp -u)
"$CLI" --budget 0 data/jag.txt "$out" >/tmp/zero.txt
zrc=$?
grep -q BUDGET_ZERO /tmp/zero.txt && say "  budget 0 -> BUDGET_ZERO (rc=$zrc)" || bad "budget 0 not reported"
[ -e "$out" ] && bad "budget 0 wrote a file"

expect_reject() {
  local desc="$1" want="$2"; shift 2
  local msg rc
  msg=$("$CLI" --check "$@" 2>&1); rc=$?
  if [ $rc -eq 2 ] && printf '%s' "$msg" | grep -q "$want"; then
    say "  REJECT ${desc}: ${msg#check *: }"
  else
    bad "check ${desc}: rc=$rc msg=$msg"
  fi
}
expect_reject "size over"  "size over"  500 500 2 100000
expect_reject "ss over"    "ss over"    200 200 4 100000
expect_reject "ss range"   "outside"    200 200 9 1000000
expect_reject "both over"  "size over"  500 500 9 1000

if [ $fail -ne 0 ]; then
  say "RESULT: FAIL"
  exit 1
fi
say "RESULT: PASS"
