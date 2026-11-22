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

# 旧路径回归：编译期关掉新逻辑（-DKEYFRAME_MIN_ACTION=0），
# 节点表与中点必须与 baseline/linear_ref.c（独立旧实现）逐字节相同。
compat: build/compat build/linear_ref
	@mkdir -p build/ref
	./build/linear_ref square2 build/ref/ref_square2.txt
	./build/linear_ref square4 build/ref/ref_square4.txt
	./build/linear_ref uneven  build/ref/ref_uneven.txt
	./build/linear_ref two     build/ref/ref_two.txt
	./build/compat build/ref

build/compat: $(SRC) compat.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DKEYFRAME_MIN_ACTION=0 $(SRC) compat.c -lm -o $@

build/linear_ref: baseline/linear_ref.c
	@mkdir -p build
	$(CC) $(CFLAGS) baseline/linear_ref.c -o $@

clean:
	rm -rf build keyframe_spans.csv ref_ours.txt

.PHONY: all test demo compat clean
