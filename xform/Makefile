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
	./build/demo

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

# 矩阵参数与 bench.c 中 M 一致，gold 参照用同一组
MAT := 0.8 -0.6 0.6 0.8 12.5 -7.25
TOL := 1e-6

# 两档点数：各报峰值内存与毫秒，并与 gold 参照逐点比对
bench: build/bench build/ptdiff ../gold/build/ref
	./build/bench gen 1000000 build/t1_in.txt
	./build/bench run build/t1_in.txt build/t1_out.txt 1024
	../gold/build/ref xform build/t1_in.txt $(MAT) > build/t1_gold.txt
	./build/ptdiff build/t1_out.txt build/t1_gold.txt $(TOL)
	./build/bench gen 4000000 build/t2_in.txt
	./build/bench run build/t2_in.txt build/t2_out.txt 1024
	../gold/build/ref xform build/t2_in.txt $(MAT) > build/t2_gold.txt
	./build/ptdiff build/t2_out.txt build/t2_gold.txt $(TOL)

build/bench: $(SRC) bench.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) bench.c -lm -o $@

build/ptdiff: ../tools/ptdiff.c
	@mkdir -p build
	$(CC) $(CFLAGS) ../tools/ptdiff.c -o $@

../gold/build/ref:
	$(MAKE) -C ../gold

clean:
	rm -rf build demo.txt demo.pgm

.PHONY: all test demo bench clean
