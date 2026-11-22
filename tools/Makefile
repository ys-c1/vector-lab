CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/cmp_points

build/cmp_points: cmp_points.c
	@mkdir -p build
	$(CC) $(CFLAGS) cmp_points.c -lm -o $@

clean:
	rm -rf build

.PHONY: all clean
