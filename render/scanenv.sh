#!/bin/sh
# 静态审查：找出会随环境变化的写法并指明位置。
#   ABS_PATH     字符串字面量里的绝对路径（/home /tmp /mnt /Users、盘符）——失败
#   LOCALE_SORT  strcoll/strxfrm/nl_langinfo、setlocale(LC_*, "")——失败
#                （qsort 比较器按数值全序比较 x/y，不走 locale，不在此列）
#   LOCALE_CTYPE isspace/isalpha… 等 LC_CTYPE 分类调用；同文件无 setlocale(LC_*, "C") 即失败
#   LOCALE_NUM   strtod/atof/strtof 与浮点 %f/%e/%g 格式化；同文件无 setlocale(LC_*, "C") 即失败
#
# 测试夹具（render/adoptlocale.c、localeprobe.c 等）按设计就是“采用环境
# locale”，文件内标注 `scanenv: harness` 即归入 HARNESS 类：照样列出来，
# 但不算违规。除此之外没有任何豁免——产品代码必须自己锁 locale。
# 用法：./scanenv.sh [源码根目录]，缺省为本仓库根。判定完全来自扫描，不靠人工。
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=${1:-$(CDPATH= cd -- "$HERE/.." && pwd)}
cd "$ROOT" || exit 2

TMPD=${TMPDIR:-/tmp}
FIND=$(mktemp "${TMPD%/}/scanenv.XXXXXX") || exit 2
trap 'rm -f "$FIND"' EXIT INT TERM

if [ -d .git ]; then
  # 已跟踪 + 未跟踪（未 ignore）都算：新写的文件不能因为还没 git add 就跳过审查。
  FILES=$( { git ls-files '*.c' '*.h' 'Makefile' '*.sh'
             git ls-files --others --exclude-standard '*.c' '*.h' 'Makefile' '*.sh'
           } 2>/dev/null | sort -u \
          | grep -v '^artifacts/' \
          | grep -vE '(^|/)render/(mkenv|renderenv|scanenv)\.sh$')
else
  FILES=$(find . -path ./.git -prune -o -path ./artifacts -prune -o \
          \( -name '*.c' -o -name '*.h' -o -name 'Makefile' \) -print \
          | sed 's|^\./||')
fi

if [ -z "$FILES" ]; then
  printf 'RESULT: FAIL (no source files found under %s)\n' "$ROOT"
  exit 1
fi

# record <cat> <loc> <verdict> <detail>
record() { printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >> "$FIND"; }

guarded() {  # 同文件是否在入口锁 C locale
  grep -qE 'setlocale *\( *LC_(ALL|NUMERIC) *, *"C" *\)' "$1" && echo yes || echo no
}

harness() { # 测试夹具显式标注
  grep -q 'scanenv: harness' "$1" 2>/dev/null && echo yes || echo no
}

# 去掉行尾注释与整行注释后再匹配，避免把注释里的说明当成代码
code_lines() {
  grep -nE "$2" "$1" 2>/dev/null | grep -vE '^[0-9]+: *([*]|/\*|//|#)'
}

nfiles=0
for f in $FILES; do
  [ -f "$f" ] || continue
  nfiles=$((nfiles+1))
  g=$(guarded "$f")

  # 绝对路径
  code_lines "$f" '"[^"]*(/home/|/tmp/|/mnt/|/Users/|[A-Za-z]:[\\/])' |
  while IFS= read -r line; do
    ln=${line%%:*}; rest=${line#*:}
    record ABS_PATH "$f:$ln" FAIL "$rest"
  done

  # 语言排序 / 显式跟随环境 locale
  h=$(harness "$f")
  code_lines "$f" '\b(strcoll|strxfrm|nl_langinfo)\b|setlocale *\( *LC_[A-Z_]+ *, *"" *\)' |
  while IFS= read -r line; do
    ln=${line%%:*}; rest=${line#*:}
    if [ "$h" = yes ]; then record HARNESS "$f:$ln" OK "test fixture adopts env locale by design: $rest"
    else record LOCALE_SORT "$f:$ln" FAIL "$rest"; fi
  done

  # 夹具里的浮点输出同理
  if [ "$h" = yes ]; then
    code_lines "$f" '\b(strtod|atof|strtof) *\(|%[-+ #0-9.*]*[efgEFG]' |
    while IFS= read -r line; do
      ln=${line%%:*}
      record HARNESS "$f:$ln" OK "test fixture prints/parses floats by design"
    done
  fi

  # LC_CTYPE
  code_lines "$f" '\b(is(space|alpha|digit|xdigit|upper|lower|alnum|punct|print|graph|cntrl|blank)) *\(' |
  while IFS= read -r line; do
    ln=${line%%:*}; rest=${line#*:}
    if [ "$g" = yes ]; then record LOCALE_CTYPE "$f:$ln" OK "guarded by setlocale(LC_*, \"C\")"
    else record LOCALE_CTYPE "$f:$ln" FAIL "$rest"; fi
  done

  # LC_NUMERIC：浮点解析与格式化（跳过纯注释行）
  [ "$h" = yes ] && continue
  code_lines "$f" '\b(strtod|atof|strtof) *\(|%[-+ #0-9.*]*[efgEFG]' |
  while IFS= read -r line; do
    ln=${line%%:*}; rest=${line#*:}
    if [ "$g" = yes ]; then record LOCALE_NUM "$f:$ln" OK "guarded by setlocale(LC_*, \"C\")"
    else record LOCALE_NUM "$f:$ln" FAIL "$rest"; fi
  done
done

viol=$(awk -F'\t' '$3=="FAIL"' "$FIND" | wc -l)
ok=$(awk -F'\t' '$3=="OK"' "$FIND" | wc -l)

printf '== 环境依赖静态审查：%s 个文件（根 %s）==\n' "$nfiles" "$ROOT"
printf '%-12s %-32s %-4s %s\n' CATEGORY LOCATION VERD DETAIL
if [ ! -s "$FIND" ]; then
  printf '  (未发现对路径/时区/语言/权限敏感的写法)\n'
else
  awk -F'\t' '$3=="FAIL" { printf "  %-12s %-32s %-4s %s\n", $1, $2, $3, $4 }' "$FIND"
  awk -F'\t' '$3=="OK"   { printf "  %-12s %-32s %-4s %s\n", $1, $2, $3, $4 }' "$FIND"
fi

printf '\n'
if [ "$viol" -gt 0 ]; then
  printf 'RESULT: FAIL (%s violations, %s guarded sites)\n' "$viol" "$ok"
  exit 1
fi
printf 'RESULT: PASS (0 violations, %s guarded sites)\n' "$ok"
exit 0
