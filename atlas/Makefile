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

build/ref: ref/ref.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) ref/ref.c -lm -o $@

crosscheck: build/cli build/ref
	bash scripts/crosscheck.sh

timing: build/cli
	bash scripts/timing.sh

demo: build/cli
	@for g in A B C D; do \
	  echo "== group$$g =="; \
	  ./build/cli data/group$$g.txt 128 128 input  > build/$$g.input.txt; \
	  ./build/cli data/group$$g.txt 128 128 smart  > build/$$g.smart.txt; \
	  tail -1 build/$$g.input.txt; tail -1 build/$$g.smart.txt; \
	done
	-./build/cli data/oversized.txt 128 128 smart; echo "reject rc=$$?"

clean:
	rm -rf build

.PHONY: all test clean demo crosscheck timing
