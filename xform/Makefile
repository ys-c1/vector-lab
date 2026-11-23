CC ?= gcc
# 优化档位：只接受 0 和 2 两档；写错立即停并指明是哪个
OPT ?= 2
ifeq ($(filter $(OPT),0 2),)
$(error 优化档位写错: OPT='$(OPT)'，合法值只有 0 和 2，例如 make OPT=0)
endif
# 严格模式（默认开）：零告警（-Werror）+ 浮点可复现（-ffp-contract=off，禁止乘加融合）
# STRICT=0 时回到旧版选项，产物与当前版本字节相同
STRICT ?= 1
ifeq ($(STRICT),1)
CFLAGS ?= -O$(OPT) -Wall -Wextra -Werror -ffp-contract=off -std=c11
else
CFLAGS ?= -O$(OPT) -Wall -Wextra -std=c11
endif
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

demo: build/demo
	./build/demo

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

clean:
	rm -rf build demo.txt demo.pgm

.PHONY: all test demo clean
