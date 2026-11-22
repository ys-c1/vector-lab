CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

# 新逻辑：凸层剥解演示（gen/run 两个子命令）。
demo: build/demo
	./build/demo

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

# 旧逻辑（新逻辑关闭）：单层凸包，产出逐字节稳定的 demo.pgm。
legacy-demo: build/legacy_demo
	./build/legacy_demo demo.pgm

build/legacy_demo: $(SRC) legacy_demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) legacy_demo.c -lm -o $@

clean:
	rm -rf build demo.txt demo.pgm

.PHONY: all test demo legacy-demo clean
