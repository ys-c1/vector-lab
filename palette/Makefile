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

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

build/tool: tool.c
	@mkdir -p build
	$(CC) $(CFLAGS) tool.c -o $@

cli: build/cli

# 生成四类图 + 单色图，然后跑聚类与旧逻辑基线。
report: build/cli build/tool
	@mkdir -p build/scenes build/legacy build/baseline
	./build/cli gen build/scenes
	./build/cli batch build/scenes/manifest.txt 8
	./build/cli legacy build/scenes/dark.ppm build/legacy/dark.ppm
	./build/cli legacy build/scenes/gradient.ppm build/legacy/gradient.ppm
	./build/cli legacy build/scenes/flat.ppm build/legacy/flat.ppm
	./build/cli legacy build/scenes/mixed.ppm build/legacy/mixed.ppm
	./build/cli legacy build/scenes/solid.ppm build/legacy/solid.ppm

# 验收：生成新场景后与冻结基线逐字节比对（基线已入库，不重算）。
# 同时用冻结实现在当前场景上重算一遍，确认基线本身还能被复现。
check-legacy: build/cli build/tool
	@mkdir -p build/scenes build/frozen build/current
	./build/cli gen build/scenes
	./build/tool record build/scenes build/frozen
	./build/cli legacy build/scenes/dark.ppm build/current/dark.ppm
	./build/cli legacy build/scenes/gradient.ppm build/current/gradient.ppm
	./build/cli legacy build/scenes/flat.ppm build/current/flat.ppm
	./build/cli legacy build/scenes/mixed.ppm build/current/mixed.ppm
	./build/cli legacy build/scenes/solid.ppm build/current/solid.ppm
	@echo "--- frozen impl vs committed baseline"
	./build/tool check build/frozen baseline
	@echo "--- current legacy path vs committed baseline"
	./build/tool check build/current baseline

clean:
	rm -rf build *.ppm *.pgm

.PHONY: all test cli report check-legacy clean
