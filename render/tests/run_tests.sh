#!/bin/bash
# rvec 端到端测试：
#   1. 六类场景错误逐一验证错误码/出错字段/blocked=yes
#   2. 子命令参数校验（E_USAGE=7 / E_FIELD=8 / E_OUTPUT=9）
#   3. 帮助输出与参数表一致性（金快照 + 选项断言）
#   4. 旧位置参数行为不变（与 git HEAD 的旧 cli 字节级比对）
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SC=$ROOT/tests/scenes
GOLD=$ROOT/tests/golden
TMP=$ROOT/tests/tmp
RVEC=$ROOT/build/rvec
LEG=$ROOT/build/rvec-legacy
mkdir -p "$TMP"
FAILS=0; CHECKS=0

# expect <子命令参数...> :: <期望退出码> <期望错误名> <期望字段>
expect() {
    local want_code="$1" want_name="$2" want_field="$3"; shift 3
    "$RVEC" "$@" >"$TMP/o.txt" 2>"$TMP/e.txt"
    local rc=$?
    CHECKS=$((CHECKS+1))
    if [ "$rc" != "$want_code" ]; then
        echo "FAIL [$*] exit=$rc want=$want_code"; FAILS=$((FAILS+1)); return
    fi
    if [ "$want_name" != "-" ] && ! grep -q "error $want_name " "$TMP/e.txt"; then
        echo "FAIL [$*] stderr missing error $want_name:"; cat "$TMP/e.txt"; FAILS=$((FAILS+1)); return
    fi
    if [ "$want_field" != "-" ] && ! grep -q "field=$want_field" "$TMP/e.txt"; then
        echo "FAIL [$*] stderr missing field=$want_field:"; cat "$TMP/e.txt"; FAILS=$((FAILS+1)); return
    fi
    if [ "$want_code" != "0" ] && ! grep -q "blocked=yes" "$TMP/e.txt"; then
        echo "FAIL [$*] missing blocked=yes:"; cat "$TMP/e.txt"; FAILS=$((FAILS+1)); return
    fi
}

ok_run() {
    "$RVEC" "$@" >"$TMP/o.txt" 2>"$TMP/e.txt"
    local rc=$?
    CHECKS=$((CHECKS+1))
    if [ "$rc" != 0 ] || [ -s "$TMP/e.txt" ]; then
        echo "FAIL [$*] rc=$rc stderr:"; cat "$TMP/e.txt"; FAILS=$((FAILS+1))
    fi
}

echo "== 1. 六类场景错误矩阵（parse 子命令） =="
# 1) E_FILE=1：打不开
expect 1 E_FILE path parse "$TMP/does_not_exist.scene"
# 2) E_VERSION=2：缺失/重复/不支持
expect 2 E_VERSION version parse "$SC/e_version_missing.scene"
expect 2 E_VERSION version parse "$SC/e_version_dup.scene"
expect 2 E_VERSION version parse "$SC/e_version_bad.scene"
# 3) E_SYNTAX=3：缺 end/未知段/段格式错
expect 3 E_SYNTAX poly.end parse "$SC/e_syntax_noend.scene"
expect 3 E_SYNTAX frobnicate parse "$SC/e_syntax_unknown.scene"
expect 3 E_SYNTAX canvas parse "$SC/e_syntax_canvas.scene"
# 4) E_MISSING=4：必填段缺失
expect 4 E_MISSING canvas parse "$SC/e_missing_canvas.scene"
expect 4 E_MISSING poly parse "$SC/e_missing_poly.scene"
# 5) E_VALUE=5：值越界/非有限/顶点不足
expect 5 E_VALUE canvas.w parse "$SC/e_value_dim.scene"
expect 5 E_VALUE canvas.scale parse "$SC/e_value_scale.scene"
expect 5 E_VALUE poly.vert parse "$SC/e_value_nan.scene"
expect 5 E_VALUE poly.verts parse "$SC/e_value_fewverts.scene"
# 6) E_CAPACITY=6：超容量
expect 6 E_CAPACITY poly parse "$SC/e_capacity_poly.scene"

echo "== 2. 错误在 render/export/diagnose 同样拦下 =="
expect 4 E_MISSING poly render "$SC/e_missing_poly.scene"
expect 5 E_VALUE canvas.scale export "$SC/e_value_scale.scene" --out "$TMP/x.pgm"
expect 3 E_SYNTAX poly.end diagnose "$SC/e_syntax_noend.scene"
expect 1 E_FILE path render "$TMP/nope.scene"
# 被拦下时不得产生输出文件
if [ -e "$TMP/x.pgm" ]; then echo "FAIL blocked export created file"; FAILS=$((FAILS+1)); fi
CHECKS=$((CHECKS+1))
# 行号必须上报（文件内错误 line>=1，文件级错误 line=0）
"$RVEC" parse "$SC/e_value_dim.scene" 2>"$TMP/e.txt"
grep -q "line=2" "$TMP/e.txt" && echo "ok line number reported" || { echo "FAIL line no:"; cat "$TMP/e.txt"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))

echo "== 3. 子命令参数校验 =="
# 无子命令：主帮助到 stdout，退出 7
"$RVEC" >"$TMP/o.txt" 2>"$TMP/e.txt"; rc=$?
CHECKS=$((CHECKS+1)); [ "$rc" = 7 ] && grep -q "usage:" "$TMP/o.txt" || { echo "FAIL no-arg behavior"; FAILS=$((FAILS+1)); }
# 未知子命令：E_USAGE
"$RVEC" bogus "$SC/ok.scene" >/dev/null 2>"$TMP/e.txt"; rc=$?
CHECKS=$((CHECKS+1)); [ "$rc" = 7 ] && grep -q "E_USAGE" "$TMP/e.txt" || { echo "FAIL unknown subcommand"; FAILS=$((FAILS+1)); }
expect 7 E_USAGE scene parse
expect 7 E_USAGE scene parse "$SC/ok.scene" "$SC/ok.scene"
expect 7 E_USAGE --nope parse --nope "$SC/ok.scene"
expect 8 E_FIELD --ss render "$SC/ok.scene" --ss 0
expect 8 E_FIELD --ss render "$SC/ok.scene" --ss 9
expect 8 E_FIELD --ss render "$SC/ok.scene" --ss abc
expect 7 E_USAGE --ss render "$SC/ok.scene" --ss
expect 7 E_USAGE --out export "$SC/ok.scene"
expect 9 E_OUTPUT --out export "$SC/ok.scene" --out "$TMP/no/such/dir/out.pgm"
# 覆盖保护：无 --force 拦下，加 --force 放行
rm -f "$TMP/cover.pgm"; : > "$TMP/cover.pgm"
expect 8 E_FIELD --out export "$SC/ok.scene" --out "$TMP/cover.pgm"
ok_run export "$SC/ok.scene" --out "$TMP/cover.pgm" --force
[ -s "$TMP/cover.pgm" ] || { echo "FAIL forced export empty"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))
# 新入口不接受旧位置参数形式
"$RVEC" "$SC/ok.scene" "$TMP/old.pgm" 16 16 1.0 >/dev/null 2>"$TMP/e.txt"; rc=$?
CHECKS=$((CHECKS+1)); [ "$rc" = 7 ] && grep -q "E_USAGE" "$TMP/e.txt" || { echo "FAIL old positional form accepted"; FAILS=$((FAILS+1)); }

echo "== 4. 帮助金快照与参数表一致性 =="
"$RVEC" --help > "$TMP/help_main.txt"
"$RVEC" parse --help > "$TMP/help_parse.txt"
"$RVEC" diagnose --help > "$TMP/help_diagnose.txt"
"$RVEC" render --help > "$TMP/help_render.txt"
"$RVEC" export --help > "$TMP/help_export.txt"
for h in main parse diagnose render export; do
    CHECKS=$((CHECKS+1))
    if cmp -s "$TMP/help_$h.txt" "$GOLD/help_$h.txt"; then :; else
        echo "FAIL help snapshot $h differs:"; diff "$GOLD/help_$h.txt" "$TMP/help_$h.txt"; FAILS=$((FAILS+1))
    fi
done
# 参数表契约断言：选项与取值区间必须出现在帮助里
assert_help() { # 文件 字符串...
    local f="$1"; shift
    for s in "$@"; do
        CHECKS=$((CHECKS+1))
        grep -qF -e "$s" "$f" || { echo "FAIL $f missing '$s'"; FAILS=$((FAILS+1)); }
    done
}
assert_help "$TMP/help_render.txt" "--ss" "[1..8]"
assert_help "$TMP/help_export.txt" "--out" "--ss" "--force" "[1..8]"
assert_help "$TMP/help_diagnose.txt" "--verbose"
assert_help "$TMP/help_parse.txt" "scene"
# parse 没有任何选项，帮助里不得出现 --
if grep -q -- "--" "$TMP/help_parse.txt"; then echo "FAIL parse help lists options"; FAILS=$((FAILS+1)); fi
CHECKS=$((CHECKS+1))
# -h 与 --help 等价
"$RVEC" -h >"$TMP/h1.txt" 2>&1; cmp -s "$TMP/h1.txt" "$TMP/help_main.txt" || { echo "FAIL -h != --help"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))

echo "== 5. 合法场景四子命令全通 =="
ok_run parse "$SC/ok.scene"
grep -q "version=1 canvas=16x16" "$TMP/o.txt" || { echo "FAIL parse output"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))
ok_run diagnose "$SC/ok.scene"
grep -q "7/7 checks passed, blocked=no" "$TMP/o.txt" || { echo "FAIL diagnose output"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))
ok_run render "$SC/ok.scene" --ss 2
grep -q "on=256 blocked=no" "$TMP/o.txt" || { echo "FAIL render output"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))
rm -f "$TMP/new.pgm"
ok_run export "$SC/ok.scene" --out "$TMP/new.pgm" --ss 2

echo "== 6. 旧位置参数行为不变 =="
# 旧格式场景（无版本号、p 开头行）
printf 'p 0 0 10 0 10 10 0 10\n' > "$TMP/legacy.txt"
# 6a. 编译期纯旧版
"$LEG" "$TMP/legacy.txt" "$TMP/leg1.pgm" 16 16 1.0 > "$TMP/leg1.out" 2>"$TMP/leg1.err"
rc=$?; CHECKS=$((CHECKS+1))
[ "$rc" = 0 ] || { echo "FAIL legacy binary rc=$rc"; cat "$TMP/leg1.err"; FAILS=$((FAILS+1)); }
# 6b. 新二进制运行期开关走同一条旧路径
RVEC_LEGACY=1 "$RVEC" "$TMP/legacy.txt" "$TMP/leg2.pgm" 16 16 1.0 > "$TMP/leg2.out" 2>&1
rc=$?; CHECKS=$((CHECKS+1))
[ "$rc" = 0 ] || { echo "FAIL RVEC_LEGACY switch rc=$rc"; FAILS=$((FAILS+1)); }
cmp -s "$TMP/leg1.pgm" "$TMP/leg2.pgm" || { echo "FAIL pgm differs between legacy entries"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))
cmp -s "$TMP/leg1.out" "$TMP/leg2.out" || { echo "FAIL stdout differs"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))
# 6c. 与 git HEAD 上的旧 cli.c 编译产物逐字节比对
git -C "$ROOT" show HEAD:render/cli.c > "$TMP/old_cli.c" 2>/dev/null
gcc -I"$ROOT" -I"$ROOT/src" -O2 -Wall -Wextra -std=c11 \
    "$ROOT"/src/export.c "$ROOT"/src/pipeline.c "$ROOT"/src/raster.c \
    "$ROOT"/src/scene.c "$ROOT"/src/scene_file.c "$TMP/old_cli.c" -lm -o "$TMP/old_cli"
"$TMP/old_cli" "$TMP/legacy.txt" "$TMP/leg3.pgm" 16 16 1.0 > "$TMP/leg3.out" 2>&1
cmp -s "$TMP/leg1.pgm" "$TMP/leg3.pgm" && cmp -s "$TMP/leg1.out" "$TMP/leg3.out" \
    || { echo "FAIL legacy output differs from git HEAD cli"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))
# 6d. 旧入口错误退出码保持 1/2
"$LEG" >/dev/null 2>&1; [ $? = 1 ] || { echo "FAIL legacy no-args code"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))
"$LEG" "$TMP/missing.scene" /dev/null 16 16 1.0 >/dev/null 2>&1; [ $? = 2 ] || { echo "FAIL legacy open-fail code"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))

echo "== 7. 边界对抗（矩阵外补充） =="
# 目录当场景 -> E_FILE
expect 1 E_FILE path parse "$TMP"
# inf 坐标 -> E_VALUE
printf 'version 1\ncanvas 16 16 1.0\npoly\n0 0\n1 0\ninf 1\nend\n' > "$TMP/inf.scene"
expect 5 E_VALUE poly.vert parse "$TMP/inf.scene"
# 65 顶点 -> E_CAPACITY；64 顶点恰好合法
{ printf 'version 1\ncanvas 8 8 1.0\npoly\n'; for i in $(seq 1 65); do echo "0 $i"; done; echo end; } > "$TMP/v65.scene"
expect 6 E_CAPACITY poly.verts parse "$TMP/v65.scene"
{ printf 'version 1\ncanvas 8 8 1.0\npoly\n'; for i in $(seq 1 64); do echo "0 $i"; done; echo end; } > "$TMP/v64.scene"
ok_run parse "$TMP/v64.scene"
# 尺寸 4097 / 负数 -> E_VALUE
printf 'version 1\ncanvas 4097 16 1.0\npoly\n0 0\n1 0\n1 1\nend\n' > "$TMP/big.scene"
expect 5 E_VALUE canvas.w render "$TMP/big.scene"
printf 'version 1\ncanvas -5 16 1.0\npoly\n0 0\n1 0\n1 1\nend\n' > "$TMP/neg.scene"
expect 5 E_VALUE canvas.w parse "$TMP/neg.scene"
# 空文件/纯注释 -> E_VERSION
: > "$TMP/empty.scene"
expect 2 E_VERSION version parse "$TMP/empty.scene"
printf '# only comments\n' > "$TMP/cmt.scene"
expect 2 E_VERSION version parse "$TMP/cmt.scene"
# 重复 canvas、end 带字段、超长行 -> E_SYNTAX
printf 'version 1\ncanvas 16 16 1.0\ncanvas 8 8 1.0\npoly\n0 0\n1 0\n1 1\nend\n' > "$TMP/dupc.scene"
expect 3 E_SYNTAX canvas parse "$TMP/dupc.scene"
printf 'version 1\ncanvas 16 16 1.0\npoly\n0 0\n1 0\n1 1\nend x\n' > "$TMP/endx.scene"
expect 3 E_SYNTAX poly.end parse "$TMP/endx.scene"
python3 -c "import sys; sys.stdout.write('version 1\ncanvas 16 16 1.0\npoly\n'+'0 '+'1 '*300+'\nend\n')" > "$TMP/long.scene"
expect 3 E_SYNTAX line parse "$TMP/long.scene"
# --ss 边界值 1 和 8 合法
ok_run render "$SC/ok.scene" --ss 1
ok_run render "$SC/ok.scene" --ss 8
# 被拦下的 export 不留输出文件
rm -f "$TMP/blocked.pgm"
"$RVEC" export "$TMP/neg.scene" --out "$TMP/blocked.pgm" >/dev/null 2>&1
[ ! -e "$TMP/blocked.pgm" ] || { echo "FAIL blocked export left file"; FAILS=$((FAILS+1)); }
CHECKS=$((CHECKS+1))

echo
echo "================ $CHECKS checks, $FAILS failures ================"
[ "$FAILS" = 0 ]
