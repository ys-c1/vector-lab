#!/bin/bash
# 回归：浮点路径（未加 --snap）必须与原始实现逐字节相同。
# 12 组基线由 git 提交 56501e4 的原始 render/cli 在 render/scenes/ 上产出，
# 哈希固定于此；若改动场景文件，需用原始实现重新生成基线。
set -u
cd "$(dirname "$0")/.." || exit 2
BIN=render/build/cli
make -C render build/cli >/dev/null 2>&1 || { echo "BUILD FAILED"; exit 2; }
tmp=$(mktemp -d) || exit 2
fail=0
n=0

while read -r scene w h sc want; do
  out=$tmp/${scene}_${w}x${h}.pgm
  "$BIN" "render/scenes/$scene.txt" "$out" "$w" "$h" "$sc" >/dev/null \
    || { echo "RUN_FAIL $scene $w $h"; fail=1; continue; }
  got=$(sha256sum "$out" | awk '{print $1}')
  n=$((n+1))
  if [ "$got" != "$want" ]; then
    echo "BYTE_DIFF $scene ${w}x${h} want=$want got=$got"
    fail=1
  fi
done <<'BASELINES'
quad 96 96 1.0 0dfa359e31df19f0bb2934a1de67ecc207fced6a0a268487aadcd664652a2802
quad 64 48 1.0 33c7c75696059449c2a831049cd6bcbda11d2f13cd97f5dfdd23e2a902ee6fd6
quad 200 120 1.0 1b677fbbbe945ec66e53b34f2f416f4d467b21a22096cdc4363038ab5ef6ade9
star 96 96 1.0 5d11d29b312b4ad47f7ea473c17cfad26ca1c962dbe906cdfa20ba11afd896f8
star 64 48 1.0 01a72bf2e28addc2673080ce4eefddc150007fc394a8600b6968dff60c9ff737
star 200 120 1.0 3cb1df336aa0a67532f247d9e20aa572baf94da6b105af572051080c165e633a
compound 96 96 1.0 7a0dc16dfa11082caaddb9f405a8eec8d0a1c685b1bbdcf5d8059f027871fe0c
compound 64 48 1.0 cf8dd817ace28cdb65f24c6a03799410aa7883321188e6bd3d2f7fb7b2329119
compound 200 120 1.0 bc0e80c58a133ea5d6e770b9833a0886564e9ea9217f05dfc51d31ee6fdd89ad
thin 96 96 1.0 bc3ed25de7e68eb280cf8edb19baaa3b9b09f29274e3e3576e3ca70dd1fcd32f
thin 64 48 1.0 6f2f5790df07c1b75deeb16c80282294fa5ebe4392fca6249a850bba40d9f542
thin 200 120 1.0 f3fb665e4e7ec5eee21aad7da2c1e4aeaba15bfaf666d7b79f1e1279a687ae44
BASELINES

# 非默认尺寸：若仓库外有原始 CLI（$ORIG_CLI），再多比几组。
ORIG_CLI=${ORIG_CLI:-/tmp/origrepo/render/build/cli}
if [ -x "$ORIG_CLI" ]; then
  for s in quad star compound thin; do
    for wh in "37 91" "160 160"; do
      set -- $wh
      "$ORIG_CLI" "render/scenes/$s.txt" "$tmp/orig.pgm" "$1" "$2" 1.0 >/dev/null 2>&1
      "$BIN" "render/scenes/$s.txt" "$tmp/new.pgm" "$1" "$2" 1.0 >/dev/null 2>&1
      n=$((n+1))
      cmp -s "$tmp/orig.pgm" "$tmp/new.pgm" || { echo "BYTE_DIFF_ORIG $s ${1}x${2}"; fail=1; }
    done
  done
fi

rm -rf "$tmp"
if [ "$fail" -eq 0 ]; then echo "FLOAT_PATH_BYTE_IDENTICAL ($n cases)"; else echo "FLOAT_PATH_REGRESSED"; fi
exit "$fail"
