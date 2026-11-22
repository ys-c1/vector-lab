CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
GOLDEN := golden/fixed_window.golden
SCENE  := golden/scene.txt

all: test

test: build/test
	./build/test
	@# 关掉新逻辑时，固定窗口渲染产物必须与既存 render CLI 的产物逐字节相同
	cmp build/legacy.pgm $(GOLDEN) \
	  && echo "BYTE-IDENTICAL legacy.pgm vs $(GOLDEN)"
	@# 若 render 模块已构建，再与实时渲染交叉比对一次
	@if [ -x ../render/build/cli ]; then \
	  ../render/build/cli $(SCENE) build/cli_recheck.pgm 48 32 1.0 >/dev/null; \
	  cmp build/legacy.pgm build/cli_recheck.pgm \
	    && echo "BYTE-IDENTICAL legacy.pgm vs live render CLI"; \
	fi

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

demo: build/demo
	./build/demo | tee demo.txt

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

clean:
	rm -rf build demo.txt legacy.pgm

.PHONY: all test demo clean
