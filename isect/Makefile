CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
DATA := A_general B_grazing C_endpoint D_snapbig

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

demo: build/demo
	./build/demo

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

build/gen: data/gen.c
	@mkdir -p build
	$(CC) $(CFLAGS) data/gen.c -lm -o $@

build/ref: gold/ref.c
	@mkdir -p build
	$(CC) $(CFLAGS) gold/ref.c -o $@

build/table: $(SRC) table.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) table.c -lm -o $@

build/sweep: $(SRC) sweep.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) sweep.c -lm -o $@

# legacy 冻结路径：只编译 legacy/ 下的原始源码
build/ltable: legacy/ltable.c legacy/isect.c legacy/isect.h
	@mkdir -p build
	$(CC) -Ilegacy $(CFLAGS) legacy/ltable.c legacy/isect.c -lm -o $@

# 全流程：生成数据 -> gold 真值 -> 交叉验证 -> 反解 -> legacy 逐字节比对 -> 硬断言
run: build/gen build/ref build/table build/sweep build/ltable
	./run.sh
	./assert.sh

clean:
	rm -rf build demo.txt data/A_general data/B_grazing data/C_endpoint data/D_snapbig report.txt
.PHONY: all test demo run clean
