CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
GOLD   := ../gold/build/ref
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

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

# 四类曲线基准：反解容差、实际弦高误差、段数、耗时；任一段超界即失败。
bench: build/bench
	./build/bench build

build/bench: $(SRC) bench.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) bench.c -lm -o $@

build/dump: $(SRC) dump.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) dump.c -lm -o $@

build/gen_baseline: $(SRC) tools/gen_baseline.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) tools/gen_baseline.c -lm -o $@

build/pgmdiff: tools/pgmdiff.c
	@mkdir -p build
	$(CC) $(CFLAGS) tools/pgmdiff.c -o $@

# 端到端：关掉新逻辑时，点序列与 PGM 必须与 baseline/ 逐字节一致。
check: build/dump build/gen_baseline build/pgmdiff $(GOLD)
	tools/render-check.sh build

$(GOLD):
	$(MAKE) -C ../gold

# 独立 Python 复算弦高误差上界 (3/4)。
verify:
	python3 -I tools/verify_bound.py

clean:
	rm -rf build demo.txt *.pgm

.PHONY: all test demo bench check verify clean
