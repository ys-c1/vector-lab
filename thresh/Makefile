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

cli: build/cli

demo: build/cli
	python3 demo/gen_demo.py demo/data
	mkdir -p demo/out
	-./build/cli batch demo/manifest.txt demo/out
	./build/cli fixed demo/data/gradient.pgm demo/out/gradient_fixed.pgm 128
	../tools/diff.sh demo/out/gradient_fixed.pgm demo/data/fixed_ref.pgm

clean:
	rm -rf build *.pgm demo/data demo/out

.PHONY: all test cli clean
