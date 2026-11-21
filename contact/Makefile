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

# 交叉验证：关闭分类时与 bezier_flatten + isect_seg 的现状管线逐位一致。
# 只读引用相邻模块源码，不改动它们。
compat: build/compat
	./build/compat

build/compat: $(SRC) compat.c $(HDR) ../bezier/src/bezier.c ../isect/src/isect.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) -I../bezier/src -I../isect/src $(CFLAGS) \
		$(SRC) ../bezier/src/bezier.c ../isect/src/isect.c compat.c -lm -o $@

clean:
	rm -rf build contact_rows_*.csv contact_hits_*.csv

.PHONY: all test demo compat clean
