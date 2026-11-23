#!/bin/bash
# 快照功能端到端验收：
#   1) 三组场景：snap -> restore，报字节数/差异像素/毫秒（差异非零即失败）
#   2) 原流程产物：当前 CLI 与 git HEAD 基线 CLI 逐字节相同
#   3) 快照关闭构建（RSNAP_DISABLE）：原流程产物仍与基线逐字节相同，
#      且 snap/restore 被拒绝
#   4) 坏版本/截断/尾随文件：restore 拒绝并给出字节偏移
set -u
cd "$(dirname "$0")"
BASE=/tmp/vlab-baseline
OUT=build/verify
rm -rf "$OUT" && mkdir -p "$OUT"
fail=0
note() { printf '\n== %s ==\n' "$1"; }
expect_fail() { if [ "$1" -ne 0 ]; then echo "OK  $2 (rejected)"; else echo "FAIL $2 should have been rejected"; fail=1; fi; }

note "build"
make build/cli build/cli-snapoff build/snapbench >/dev/null || exit 2

note "benchmark: bytes / diff pixels / ms"
./build/snapbench scenes || fail=1

note "legacy pipeline byte-identical to git HEAD baseline"
set -- "s1_square 64 64 1.0" "s2_two 80 80 1.2" "s3_three 96 96 0.8"
for spec in "$@"; do
  set -- $spec
  sc="scenes/$1.txt"
  "$BASE/cli-old"        "$sc" "$OUT/$1.old.pgm"    $2 $3 $4
  ./build/cli            "$sc" "$OUT/$1.new.pgm"    $2 $3 $4
  ./build/cli-snapoff    "$sc" "$OUT/$1.off.pgm"    $2 $3 $4
  ../tools/diff.sh "$OUT/$1.old.pgm" "$OUT/$1.new.pgm" || fail=1
  ../tools/diff.sh "$OUT/$1.old.pgm" "$OUT/$1.off.pgm" || fail=1
done

note "snap/restore round trip via CLI"
for spec in "s1_square 64 64 1.0" "s2_two 80 80 1.2" "s3_three 96 96 0.8"; do
  set -- $spec
  sc="scenes/$1.txt"
  ./build/cli snap "$sc" "$OUT/$1.snap" $2 $3 $4
  ./build/cli restore "$OUT/$1.snap" "$OUT/$1.restored.pgm"
  ../tools/diff.sh "$OUT/$1.new.pgm" "$OUT/$1.restored.pgm" || fail=1
done

note "snap with explicit transform + window"
./build/cli snap scenes/s3_three.txt "$OUT/s3c.snap" 96 96 0.8 -3 4 0.6 -12 -10 12 12
# 同一参数直接渲染一份做参照
./build/cli restore "$OUT/s3c.snap" "$OUT/s3c.pgm"
./build/cli snap scenes/s3_three.txt "$OUT/s3c2.snap" 96 96 0.8 -3 4 0.6 -12 -10 12 12
cmp -s "$OUT/s3c.snap" "$OUT/s3c2.snap" && echo "OK  snapshots deterministic" || { echo "FAIL nondeterministic snapshot"; fail=1; }

note "feature disabled build rejects snapshot ops"
set +e
./build/cli-snapoff snap scenes/s1_square.txt "$OUT/x.snap" 16 16 1.0 2>"$OUT/err1"; rc1=$?
./build/cli-snapoff restore "$OUT/s1_square.snap" "$OUT/x.pgm" 2>"$OUT/err2"; rc2=$?
set -e
expect_fail "$rc1" "snap under RSNAP_DISABLE"
expect_fail "$rc2" "restore under RSNAP_DISABLE"
grep -q "disabled" "$OUT/err1" && echo "OK  disable message" || { echo "FAIL missing disable message"; fail=1; }

note "corrupt snapshots rejected with byte offset"
cp "$OUT/s1_square.snap" "$OUT/badver.snap"
printf '\x02' | dd of="$OUT/badver.snap" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
set +e
./build/cli restore "$OUT/badver.snap" "$OUT/bad.pgm" 2>"$OUT/e_badver"; rc=$?
set -e
expect_fail "$rc" "bad version"
grep -q "offset 4" "$OUT/e_badver" && echo "OK  version offset reported" || { echo "FAIL offset"; cat "$OUT/e_badver"; fail=1; }

sz=$(stat -c %s "$OUT/s1_square.snap")
half=$((sz / 2))
head -c "$half" "$OUT/s1_square.snap" > "$OUT/trunc.snap"
set +e
./build/cli restore "$OUT/trunc.snap" "$OUT/bad.pgm" 2>"$OUT/e_trunc"; rc=$?
set -e
expect_fail "$rc" "truncated file"
grep -Eq "offset $half" "$OUT/e_trunc" && echo "OK  truncation offset reported" || { echo "FAIL trunc offset"; cat "$OUT/e_trunc"; fail=1; }

cp "$OUT/s1_square.snap" "$OUT/trail.snap"
printf '\x00\x00\x00' >> "$OUT/trail.snap"
set +e
./build/cli restore "$OUT/trail.snap" "$OUT/bad.pgm" 2>"$OUT/e_trail"; rc=$?
set -e
expect_fail "$rc" "trailing bytes"
grep -Eq "offset $sz" "$OUT/e_trail" && echo "OK  trailing offset reported" || { echo "FAIL trail offset"; cat "$OUT/e_trail"; fail=1; }

printf 'NOPE' > "$OUT/magic.snap"
cat "$OUT/s1_square.snap" | tail -c +5 >> "$OUT/magic.snap"
set +e
./build/cli restore "$OUT/magic.snap" "$OUT/bad.pgm" 2>"$OUT/e_magic"; rc=$?
set -e
expect_fail "$rc" "bad magic"
grep -q "offset 0" "$OUT/e_magic" && echo "OK  magic offset reported" || { echo "FAIL magic offset"; cat "$OUT/e_magic"; fail=1; }

note "result"
if [ "$fail" -eq 0 ]; then echo "ALL VERIFY PASS"; else echo "VERIFY FAIL"; fi
exit "$fail"
