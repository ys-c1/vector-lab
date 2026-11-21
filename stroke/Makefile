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

demo: build/demo
	./build/demo | tee demo.txt

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

# 基准：8 / 64 拐点两组 × 四策略，输出面积、点数、耗时并导出选择表 CSV
bench: build/bench
	@mkdir -p bench/build
	./build/bench bench/build/selection.csv | tee bench/build/bench.txt

build/bench: $(SRC) bench/bench.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) bench/bench.c -lm -o $@

# 独立 Python 参考实现逐点核对（含 L 形手算三接法与选择表核对）
verify: bench/build/dump
	python3 -I bench/ref_check.py

bench/build/dump: $(SRC) dump.c $(HDR)
	@mkdir -p bench/build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) dump.c -lm -o $@

clean:
	rm -rf build demo.txt bench/build

.PHONY: all test demo bench verify clean
