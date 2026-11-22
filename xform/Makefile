CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

# 拦截内置展开，保证 memcpy/memmove/posix_memalign 等调用真正经过 --wrap
BENCH_CFLAGS := $(CFLAGS) -fno-builtin
WRAP := -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc \
        -Wl,--wrap=aligned_alloc -Wl,--wrap=posix_memalign \
        -Wl,--wrap=free -Wl,--wrap=memcpy -Wl,--wrap=memmove \
        -Wl,--wrap=mempcpy

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

bench: build/bench
	@echo "usage: ./build/bench sweep|mem|copy|verify dense|sparse|nan|long [reps]"

build/bench: $(SRC) bench/bench.c bench/shim.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(BENCH_CFLAGS) $(SRC) bench/bench.c bench/shim.c \
		$(WRAP) -lm -o $@

demo: build/demo
	./build/demo

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

clean:
	rm -rf build demo.txt demo.pgm

.PHONY: all test demo bench clean
