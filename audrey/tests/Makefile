# Bench check, a board in update mode (Mac or Linux, needs libusb): what Windows
# reads to install WinUSB by itself (../../specs/audrey-bootloader.md, "Windows").
#   make -C audrey/tests probe
CC     ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O1

LIBUSB_CFLAGS = $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS   = $(shell pkg-config --libs libusb-1.0 2>/dev/null || echo -lusb-1.0)

all: probe

build/winusb_probe: winusb_probe.c
	mkdir -p build
	$(CC) $(CFLAGS) $(LIBUSB_CFLAGS) $< -o $@ $(LIBUSB_LIBS)

probe: build/winusb_probe
	./build/winusb_probe

clean:
	rm -rf build

.PHONY: all probe clean
