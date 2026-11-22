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

# 兼容性：把 HEAD 版本的 svgpath.c 原样编进来，与新实现逐字节对比。
# 只在测试里引用，不参与默认构建。
compat: build/compat
	./build/compat

build/compat: $(SRC) compat/compat.c compat/legacy_svgpath.c compat/legacy_shim.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) -Icompat $(CFLAGS) -Wno-missing-field-initializers \
		$(SRC) compat/legacy_shim.c compat/compat.c -lm -o $@

clean:
	rm -rf build demo.txt demo*.pgm

.PHONY: all test demo compat clean
