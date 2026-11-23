#!/bin/bash
# 元数据核对：把 .meta.json 的每个字段与独立来源的实际值逐项比对，
# 任何偏差或字段缺失/类型错误即失败。
# 用法：metacheck.sh <scene.txt> <out.pgm> <out.meta.json> <seq-file> <expect-seq>
set -u
scene=$1 pgm=$2 meta=$3 seqf=$4 expect_seq=$5
fail=0
err() { echo "MISMATCH $meta: $*"; fail=1; }

[ -f "$meta" ] || { echo "MISSING $meta"; exit 2; }

# 实际值 1：画幅取自 PGM 头（P5 第二行 "w h"），独立于渲染代码
read -r pw ph < <(awk 'NR==2 {print $1, $2}' "$pgm")
# 实际值 2：顶点总数直接数场景文件里的坐标对（每行 p 后跟 2n 个数字）
pverts=$(awk '{for (i=2; i<=NF; i+=2) n++} END {print n+0}' "$scene")
# 实际值 3：序号文件第一行
slast=$(awk 'NR==1 {print $1}' "$seqf")

get() { sed -n "s/^  \"$1\": \\([0-9][0-9]*\\),\{0,1\}$/\1/p" "$meta"; }
mw=$(get width); mh=$(get height); mv=$(get vertices); ms=$(get sequence); me=$(get elapsed_ms)

# 字段缺失/非整数：get 输出为空即拒
for pair in "width=$mw" "height=$mh" "vertices=$mv" "sequence=$ms" "elapsed_ms=$me"; do
  k=${pair%%=*}; v=${pair#*=}
  case "$v" in ''|*[!0-9]*) err "field $k missing or not an integer (got '$v')";; esac
done

[ "$mw" = "$pw" ] || err "width meta=$mw actual=$pw"
[ "$mh" = "$ph" ] || err "height meta=$mh actual=$ph"
[ "$mv" = "$pverts" ] || err "vertices meta=$mv actual=$pverts"
[ "$ms" = "$expect_seq" ] || err "sequence meta=$ms expected=$expect_seq"
[ "$ms" = "$slast" ] || err "sequence meta=$ms not persisted (seq file=$slast)"
[ "$me" -ge 0 ] 2>/dev/null || err "elapsed_ms not a non-negative integer: $me"

# 固定字段顺序核对
order=$(sed -n 's/^  "\([a-z_]*\)":.*/\1/p' "$meta" | tr '\n' ' ')
[ "$order" = "width height vertices sequence elapsed_ms " ] || err "field order wrong: $order"

if [ "$fail" -eq 0 ]; then
  echo "OK $meta: ${pw}x${ph} verts=$pverts seq=$ms elapsed=${me}ms (zero deviation)"
fi
exit $fail
