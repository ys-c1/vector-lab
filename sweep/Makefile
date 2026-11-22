CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c sets.h $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

demo: build/demo
	@mkdir -p build
	./build/demo build/sweep_groups.csv build/sweep_hits.csv build/sweep_legacy_hits.csv build/sweep_windows.csv

build/demo: $(SRC) demo.c sets.h $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

# 交叉验证：关闭合并窗口时，交点表与独立逐事件参照 + isect_seg 逐字节一致。
compat: build/compat
	./build/compat

build/compat: $(SRC) compat.c sets.h $(HDR) ../isect/src/isect.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) -I../isect/src $(CFLAGS) \
		$(SRC) ../isect/src/isect.c compat.c -lm -o $@

# 一致性基线：保存逐事件交点表，之后 make check-baseline 逐字节复核。
# （baseline/sweep_legacy_hits.txt 由 demo 的逐事件路径生成。）
baseline: build/demo
	./build/demo baseline/sweep_groups.txt baseline/sweep_hits.txt baseline/sweep_legacy_hits.txt baseline/sweep_windows.txt > /dev/null
	@ls -l baseline/sweep_legacy_hits.txt

check-baseline: build/demo
	@mkdir -p build
	./build/demo build/sweep_groups.txt build/sweep_hits.txt build/sweep_legacy_hits.txt build/sweep_windows.txt > /dev/null
	@cmp baseline/sweep_legacy_hits.txt build/sweep_legacy_hits.txt && \
		echo "IDENTICAL baseline/sweep_legacy_hits.txt build/sweep_legacy_hits.txt"

clean:
	rm -rf build

.PHONY: all test demo compat baseline check-baseline clean
