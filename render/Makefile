CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
# 快照功能关闭时：-DRSNAP_DISABLE，快照代码退化为桩，原渲染流程不受影响。
SNAP_OFF_CPPFLAGS := $(CPPFLAGS) -DRSNAP_DISABLE
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

build/cli-snapoff: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(SNAP_OFF_CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

build/snapbench: $(SRC) snapbench.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) snapbench.c -lm -o $@

bench: build/snapbench
	./build/snapbench scenes

cli: build/cli

clean:
	rm -rf build *.pgm *.snap

.PHONY: all test bench cli clean
