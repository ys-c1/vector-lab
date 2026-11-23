CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
# 新入口与旧入口都含 main，构建二进制时各自排除对方。
LIB_SRC := $(filter-out src/cli.c,$(SRC))

all: test cli

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIB_SRC) test.c -lm -o $@

# 新子命令入口（parse/render/export/diagnose），保留运行期旧用法开关 RVEC_LEGACY=1
build/rvec: $(SRC) $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -lm -o $@

# 编译期关掉新入口的纯旧版：只认位置参数 scene.txt out.pgm w h scale
build/rvec-legacy: $(SRC) $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRVEC_LEGACY_ONLY $(SRC) -lm -o $@

cli: build/rvec build/rvec-legacy

# 端到端：六类错误矩阵、帮助快照、新旧入口行为一致性
check-cli: build/rvec build/rvec-legacy
	./tests/run_tests.sh

clean:
	rm -rf build *.pgm tests/tmp

.PHONY: all test cli check-cli clean
