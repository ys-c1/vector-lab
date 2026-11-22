CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc -D_POSIX_C_SOURCE=199309L
LDLIBS := -lz -lm
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c $(LDLIBS) -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c $(LDLIBS) -o $@

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c $(LDLIBS) -o $@

demo: build/demo build/cli
	@mkdir -p build/img
	./build/demo build/img
	./build/cli bench build/img/*.pgm

clean:
	rm -rf build *.sbc

.PHONY: all test demo clean
