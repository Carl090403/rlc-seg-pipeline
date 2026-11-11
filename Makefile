CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -D_POSIX_C_SOURCE=200809L
INC      = -Iinclude
SRC      = src/crc32.c src/segmenter.c src/bqueue.c src/pipeline.c
LDLIBS   = -pthread

.PHONY: all test tsan asan bench clean
all: build/test_main build/bench

build/test_main: $(SRC) tests/test_main.c | build
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDLIBS)

build/bench: $(SRC) bench/bench.c | build
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDLIBS)

build:
	mkdir -p build

test: build/test_main
	./build/test_main

tsan: | build
	$(CC) $(CFLAGS) -O1 -fsanitize=thread $(INC) $(SRC) tests/test_main.c -o build/test_tsan $(LDLIBS) && ./build/test_tsan

asan: | build
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(INC) $(SRC) tests/test_main.c -o build/test_asan $(LDLIBS) && ./build/test_asan

bench: build/bench
	./build/bench

clean:
	rm -rf build
