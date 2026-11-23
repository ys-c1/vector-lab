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

# 可复现性验证用的固定场景与画布参数
SCENE := scene.txt
RW := 96
RH := 64
RSCALE := 1.5

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

# 两档优化各编一份 CLI，渲染同一场景，逐字节比较（PGM 头相同，字节差即像素差）
repro:
	@$(MAKE) --no-print-directory build/cli_O0 OPT=0 STRICT=$(STRICT)
	@$(MAKE) --no-print-directory build/cli_O2 OPT=2 STRICT=$(STRICT)
	./build/cli_O0 $(SCENE) build/repro_O0.pgm $(RW) $(RH) $(RSCALE)
	./build/cli_O2 $(SCENE) build/repro_O2.pgm $(RW) $(RH) $(RSCALE)
	@cmp build/repro_O0.pgm build/repro_O2.pgm && \
		echo "REPRO OK: OPT=0 与 OPT=2 逐像素零差异"

build/cli_O0 build/cli_O2: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

clean:
	rm -rf build *.pgm

.PHONY: all test repro clean
