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

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

build/guardcheck: $(SRC) guardcheck.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) guardcheck.c -lm -o $@

# Sentinel acceptance run: repeated renders, two sentinel values, differential
# detection against the pinned pre-change images.
guard: build/guardcheck build/cli
	./build/guardcheck

clean:
	rm -rf build *.pgm

.PHONY: all test guard clean
