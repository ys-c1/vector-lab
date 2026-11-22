CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test build/dagtest build/lscenetest
	./build/test
	./build/dagtest
	./build/lscenetest

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

build/layercli: $(SRC) layercli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) layercli.c -lm -o $@

build/dagtest: src/dag.c src/dag.h dag_test.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/dag.c dag_test.c -lm -o $@

build/lscenetest: $(SRC) lscene_test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) lscene_test.c -lm -o $@

dagtest: build/dagtest
	./build/dagtest

layercli: build/layercli

layers: build/layercli
	./run_layers.sh

clean:
	rm -rf build *.pgm

.PHONY: all test clean layercli dagtest layers
