# Host test (no board needed): make -C audrey/tests
CC     ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O1 -I..

all: run

build/sw2_hold_test: sw2_hold_test.c ../sw2_hold.h ../boot_layout.h
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@

run: build/sw2_hold_test
	./build/sw2_hold_test

clean:
	rm -rf build

.PHONY: all run clean
