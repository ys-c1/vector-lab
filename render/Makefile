CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
# 确定性构建标志：
#   -ffp-contract=off  禁用乘加（FMA）合并，杜绝 a*b+c 被融合成单条 FMA
#   -fno-fast-math     不开启会重排/放宽浮点的快速数学（默认即关，显式钉死）
# 不使用 -ffast-math / -funsafe-math-optimizations / -march 原生 FMA 融合路径。
DET_CFLAGS := -O2 -Wall -Wextra -std=c11 -ffp-contract=off -fno-fast-math
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test build/test_fpconf
	./build/test
	./build/test_fpconf

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/test_fpconf: src/fpconf.c src/fpconf.h test_fpconf.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/fpconf.c test_fpconf.c -o $@

# 基线 CLI：沿用历史 CFLAGS，产物必须与改动前逐字节一致。
build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

# 确定性 CLI：用 DET_CFLAGS 构建。
build/cli-det: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(DET_CFLAGS) $(SRC) cli.c -lm -o $@

# 一次构建两套 CLI，供确定性脚本比对。
clis: build/cli build/cli-det

clean:
	rm -rf build *.pgm fpconf_test

.PHONY: all test clean clis
