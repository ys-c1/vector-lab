CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test build/test_simplify
	./build/test
	./build/test_simplify

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/test_simplify: $(SRC) test_simplify.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test_simplify.c -lm -o $@

demo: build/demo
	./build/demo

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

simp: build/demo_simplify
	./build/demo_simplify data

build/demo_simplify: $(SRC) demo_simplify.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo_simplify.c -lm -o $@

build/dump_simp: $(SRC) dump_simp.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) dump_simp.c -lm -o $@

build/dump_hull: $(SRC) dump_hull.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) dump_hull.c -lm -o $@

clean:
	rm -rf build demo.txt

.PHONY: all test demo clean
