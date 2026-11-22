CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
REF := ../gold/build/ref
CMP := ../tools/build/cmp_points

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

demo: build/demo
	./build/demo out

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

$(REF) $(CMP):
	$(MAKE) -C ../gold
	$(MAKE) -C ../tools build/cmp_points

# 端到端：两档上界，逐段偏差、总段数、均匀基线对比、落盘点与 gold 对齐
check: build/demo $(REF) $(CMP)
	./build/demo out

clean:
	rm -rf build out *.txt
	rm -f ../tools/build/cmp_points

.PHONY: all test demo check clean
