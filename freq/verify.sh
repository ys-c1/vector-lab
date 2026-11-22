#!/bin/bash
# 端到端验证：四类纹理的截止/保留/耗时/模糊度，两类拒绝，纯色单列，
# 以及“关掉新逻辑时逐字节不变”。
set -u
cd "$(dirname "$0")"

echo "==================== 1. 单元与集成测试 ===================="
make -s test || exit 1

echo
echo "==================== 2. 四类纹理：截止 / 保留 / 耗时 / 模糊度 ===================="
make -s demo || exit 1

echo
echo "==================== 3. 耗时分解 ===================="
make -s bench || exit 1

echo
echo "==================== 4. 空域路径逐字节锁定 ===================="
make -s compat || exit 1

echo
echo "==================== 5. 编译期关闭频域逻辑 ===================="
# FQ_ENABLE_FREQ=0 时 fq_run 输出输入副本，并返回 FQ_E_DISABLED。
mkdir -p run-out
gcc -O2 -Wall -Wextra -std=c11 -DFQ_ENABLE_FREQ=0 -Isrc \
    src/freq.c src/fft.c demo.c -lm -o run-out/demo_off || exit 1
./run-out/demo_off > run-out/off.txt 2>&1
grep -q '关闭新逻辑' run-out/off.txt && echo "编译期关闭：demo 正常返回" || {
    echo "编译期关闭路径异常"; exit 1; }

echo
echo "==================== 6. 逐字节对照（关新逻辑 vs 空域） ===================="
./build/compat || exit 1

echo
echo "全部通过。产物：demo_*.pgm（图像）、run-out/（对照）。"
