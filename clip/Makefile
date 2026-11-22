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

run: build/ledger
	@echo "ledger ready: ./build/ledger plan tests/ledger/plan.txt <outdir>"

build/ledger: $(SRC) ledger.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) ledger.c -lm -o $@

# 故障注入构建：裁剪输出被轻微缩放，用来验证账目能停在正确的步号
build/ledger_fault: ledger.c tests/fault_clip.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) -DCLIP_FN=clip_convex_impl $(CFLAGS) src/clip.c ledger.c tests/fault_clip.c -lm -o $@

verify: build/ledger build/ledger_fault
	./verify.sh

clean:
	rm -rf build demo.txt run-out ledger-out

.PHONY: all test demo run verify clean
