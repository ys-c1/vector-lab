#!/bin/bash
# sweep 全量验证：
#  1. 四组线段集各报排序键、十次重跑一致次数、耗时（stable 与 legacy 对照）
#  2. stable 十次重跑逐字节一致；legacy 用不同种子演示不稳定
#  3. 键相同但段不同的分组（ties）
#  4. 非法输入（零长/重复段）拒绝并指出段号
#  5. 关闭新逻辑（legacy）时交点表的集合内容与 stable 相同（canon 逐字节）
set -u
cd "$(dirname "$0")"
B=build/bench
R=build/reports
mkdir -p "$R"
SETS=(data/set1_basic.txt data/set2_ties.txt data/set3_concurrent.txt data/set4_grid.txt)

make build/bench >/dev/null

echo "================ 1. 排序键（stable 全序键，前 12 行/组） ================"
for s in "${SETS[@]}"; do
  name=$(basename "$s" .txt)
  echo "---- $name ----"
  "$B" events "$s" --stable | tee "$R/${name}.events.txt" | head -13
done

echo
echo "================ 2. 十次重跑：一致次数 / 不同顺序数 / 耗时 ================"
for s in "${SETS[@]}"; do
  name=$(basename "$s" .txt)
  "$B" rerun "$s" --stable --runs 10 | tee "$R/${name}.rerun.stable.txt"
  "$B" rerun "$s" --seed 1 --runs 10        | tee "$R/${name}.rerun.legacy.txt"
done

echo
echo "================ 3. 十次重跑逐字节比对（stable） ================"
for s in "${SETS[@]}"; do
  name=$(basename "$s" .txt)
  for r in $(seq 1 10); do "$B" table "$s" --stable > "$R/byte.$name.$r.txt"; done
  ok=1
  for r in $(seq 2 10); do
    if ! cmp -s "$R/byte.$name.1.txt" "$R/byte.$name.$r.txt"; then ok=0; fi
  done
  [ $ok -eq 1 ] && echo "IDENTICAL x10  $name" || echo "DIFF           $name"
done

echo
echo "================ 4. legacy 不同种子复现不稳定（交点行顺序） ================"
for s in "${SETS[@]}"; do
  name=$(basename "$s" .txt)
  "$B" table "$s" --seed 111 > "$R/$name.legacy.a.txt"
  "$B" table "$s" --seed 999 > "$R/$name.legacy.b.txt"
  if cmp -s "$R/$name.legacy.a.txt" "$R/$name.legacy.b.txt"; then
    echo "same-order     $name（本组无同位置事件块，旧排序恰好稳定）"
  else
    off=$(cmp -l "$R/$name.legacy.a.txt" "$R/$name.legacy.b.txt" | head -1 | awk '{print $1}')
    echo "DIFFER         $name 首个差异字节=$off"
  fi
done

echo
echo "================ 5. 键相同但段不同的额外比较（ties） ================"
for s in "${SETS[@]}"; do
  name=$(basename "$s" .txt)
  echo "---- $name ----"
  "$B" ties "$s" | tee "$R/${name}.ties.txt"
done

echo
echo "================ 6. 非法输入拒绝（段号） ================"
"$B" table data/bad1_zero_length.txt --stable; echo "exit=$?"
"$B" table data/bad2_duplicate.txt   --stable; echo "exit=$?"

echo
echo "================ 7. 关闭新逻辑：交点表集合逐字节不变（规范化后比对） ================"
for s in "${SETS[@]}"; do
  name=$(basename "$s" .txt)
  # stable 规范化交点行
  "$B" canon "$s" | grep -v '^canon' | sort > "$R/$name.canon.sorted.txt"
  # legacy 交点行抽成同一格式 "x y a b" 后规范化
  # 行形如: row   0 x= 2.000000 y= 0.000000 a=0 b=2
  "$B" table "$s" --seed 7 | grep '^row' \
    | sed 's/^row *[0-9]* //; s/x=//g; s/y=//g; s/a=//g; s/b=//g' \
    | awk '{print $1, $2, $3, $4}' \
    | sort > "$R/$name.legacy.sorted.txt"
  if cmp -s "$R/$name.canon.sorted.txt" "$R/$name.legacy.sorted.txt"; then
    echo "IDENTICAL set  $name（legacy 与 stable 交点集合逐字节相同，仅事件次序不同）"
  else
    echo "DIFF set       $name"; diff "$R/$name.canon.sorted.txt" "$R/$name.legacy.sorted.txt" | head
  fi
done
