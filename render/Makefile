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

check: build/cli
	bash check_float.sh

scene: build/cli
	@mkdir -p build/report
	@for s in quad star compound thin; do \
	  echo "==== $$s"; \
	  ./build/cli scenes/$$s.txt build/report/$$s.pgm 96 96 1.0 --snap; \
	done

clean:
	rm -rf build *.pgm

.PHONY: all test check scene clean
