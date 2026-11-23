CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
# 固定展开顺序：wildcard 按目录读序返回，文件系统不同（复制、解压、CI 缓存）
# 可能给出不同顺序，进而得到不同二进制。sort 让链接顺序与文档顺序一致。
SRC := $(sort $(wildcard src/*.c))
HDR := $(sort $(wildcard src/*.h))
# makelocale 会随环境变（LANG=de_DE 时 Gnu make 也可能把输出改成逗号小数点），
# 统一走 C locale；-s 静默，避免每条命令都打印目录名（目录名可能带路径）。
export LC_ALL := C
MAKEFLAGS += -s

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

build/pgmdiff: pgmdiff.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) pgmdiff.c -lm -o $@

verify: build/cli build/pgmdiff
	./scanenv.sh
	./mkenv.sh
	./renderenv.sh

clean:
	rm -rf build *.pgm

.PHONY: all test verify clean
