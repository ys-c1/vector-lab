CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
# 旧管线源码，用于"关掉新逻辑时逐字节一致"的比对
RSRC := ../render/src/scene.c ../render/src/raster.c ../render/src/pipeline.c

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c groups.h $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) $(RSRC) test.c -lm -o $@

demo: build/demo
	./build/demo

build/demo: $(SRC) demo.c groups.h $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

clean:
	rm -rf build

.PHONY: all test demo clean
