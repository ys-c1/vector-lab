CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test build/test_simplify build/cli
	./build/test
	./build/test_simplify
	../tools/simplify-check.sh build/cli

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/test_simplify: $(SRC) test_simplify.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test_simplify.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

bench: build/bench
	./build/bench

build/bench: $(SRC) bench.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) bench.c -lm -o $@

clean:
	rm -rf build *.pgm

.PHONY: all test bench clean
