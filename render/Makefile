CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test build/oom_test
	./build/test
	./build/oom_test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/oom_test: $(SRC) oom_test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free \
		$(SRC) oom_test.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

build/bench: $(SRC) bench.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) bench.c -lm -o $@

bench: build/cli build/bench
	@../tools/bench.sh

clean:
	rm -rf build *.pgm

.PHONY: all test bench clean
