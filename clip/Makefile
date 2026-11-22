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

run: build/run
	@echo "driver ready: ./build/run clip|cmp ..."

build/run: $(SRC) run.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) run.c -lm -o $@

# 故障注入：工作内存膨胀的裁剪实现，用于验证四倍预算拒绝路径
build/run_fault: run.c tests/fault_clip.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/fault_clip.c run.c -lm -o $@

clean:
	rm -rf build demo.txt run-out

.PHONY: all test demo run clean
