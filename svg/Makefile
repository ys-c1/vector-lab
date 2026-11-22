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

# 新逻辑（多层轮廓）：独立开关，默认不参与 test/demo
test-layers: build/test-layers
	./build/test-layers

build/test-layers: $(SRC) test_layers.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSVGPATH_MULTILAYER $(SRC) test_layers.c -lm -o $@

demo-layers: build/demo-layers
	./build/demo-layers

build/demo-layers: $(SRC) demo_layers.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSVGPATH_MULTILAYER $(SRC) demo_layers.c -lm -o $@

# 旧路径回归：新逻辑默认关闭，demo.pgm 必须与基线逐字节相同
regress: demo
	../tools/diff.sh demo.pgm baseline/demo.pgm

clean:
	rm -rf build demo.txt demo.pgm demo-layers.pgm

.PHONY: all test demo test-layers demo-layers regress clean
