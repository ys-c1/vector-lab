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

bench: build/bench
	./build/bench

build/bench: $(SRC) bench.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) bench.c -lm -o $@

demo: build/demo
	./build/demo

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

# 兼容性：把空域路径在固定输入下锁成 baseline，改动频域逻辑后必须逐字节复现
compat: build/compat
	./build/compat

build/compat: $(SRC) compat.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) compat.c -lm -o $@

clean:
	rm -rf build *.pgm run-out baseline_tmp
	@echo "注意：baseline/ 是受版本控制的参照，不随 clean 删除"

.PHONY: all test bench demo compat clean
