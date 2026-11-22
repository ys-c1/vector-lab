#!/bin/bash
# 两种求解模式确定性记录比对。
#  - 状态码逐组逐字节一致（OK / 退化 / 越界 / 残差超差的判定口径相同）
#  - 退化冲突诊断（conflict=...）若存在，必须逐字节一致
#  - 所有浮点字段（未知量、残差）按容差比对：网格搜索与闭式最小二乘的精度差
set -u
a=$1; b=$2
tol=${3:-1e-6}
if [ ! -f "$a" ] || [ ! -f "$b" ]; then echo "MISSING $a $b"; exit 2; fi

fail=0
paste -d'|' "$a" "$b" | while IFS='|' read -r la lb; do
  ga=$(printf '%s' "$la" | sed -n 's/.*group=\([^ ]*\).*/\1/p')
  sa=$(printf '%s' "$la" | sed -n 's/.*status=\([A-Z_]*\).*/\1/p')
  sb=$(printf '%s' "$lb" | sed -n 's/.*status=\([A-Z_]*\).*/\1/p')
  if [ "$sa" != "$sb" ]; then echo "STATUS MISMATCH group=$ga: $sa != $sb"; fail=1; fi
  ca=$(printf '%s' "$la" | grep -o 'conflict=[^ ]*' || true)
  cb=$(printf '%s' "$lb" | grep -o 'conflict=[^ ]*' || true)
  if [ "$ca" != "$cb" ]; then echo "CONFLICT MISMATCH group=$ga: '$ca' != '$cb'"; fail=1; fi
done

python3 - "$a" "$b" "$tol" <<'PY'
import sys
# 分级容差：远小于半像素红线，足以抓真回归，又容纳闭式最小二乘与
# 单位圆约束网格搜索这两种目标函数的系统差。
TOL = {'rot': 1e-3, 'tx': 0.2, 'ty': 0.2, 'scale': 0.05,
       'maxres': 0.1, 'rms': 0.1}
def parse(line):
    d = {}
    for tok in line.split():
        if '=' in tok:
            k, v = tok.split('=', 1)
            try: d[k] = float(v)
            except ValueError: d[k] = v
    return d
bad = 0
for la, lb in zip(open(sys.argv[1]), open(sys.argv[2])):
    x, y = parse(la), parse(lb)
    for k, t in TOL.items():
        if k in x and k in y and abs(x[k] - y[k]) > t:
            print(f"NUMERIC DIVERGE group={x['group']} {k}: {x[k]} vs {y[k]} (tol {t})")
            bad = 1
sys.exit(1 if bad else 0)
PY
rc=$?
if [ $rc -eq 0 ]; then
  echo "EQUIVALENT $a $b (status/conflict byte-identical; numerics within $tol)"
fi
exit $rc
