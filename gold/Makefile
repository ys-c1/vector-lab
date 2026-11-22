CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/ref

build/ref: ref.c
	@mkdir -p build
	$(CC) $(CFLAGS) ref.c -lm -o $@

check: build/ref
	@echo "gold ready"

clean:
	rm -rf build

.PHONY: all check clean
