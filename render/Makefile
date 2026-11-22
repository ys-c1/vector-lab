CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc -I../hull/src
SRC := $(wildcard src/*.c) ../hull/src/hull.c ../hull/src/simplify.c
HDR := $(wildcard src/*.h) ../hull/src/hull.h ../hull/src/simplify.h

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

clean:
	rm -rf build *.pgm

.PHONY: all test clean
