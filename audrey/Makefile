# USB DFU bootloader for the Audrey integrated board (STM32H743VIT6).
# See ../specs/audrey-bootloader.md.
#
#   make              build build/audrey_bootloader.bin
#   make program      flash and verify with an ST-Link
#   make program-dfu  flash through ST's ROM DFU (hold SW1 at power-on)

TARGET = audrey_bootloader

LIBDAISY_DIR = ../libDaisy
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

CPP_STANDARD = -std=c++17
OPT = -O2
LDSCRIPT = boot_linker.lds
FLASH_ADDRESS = 0x08000000

CPP_SOURCES = main.cpp board.cpp dfu_flash.cpp led_status.cpp ws2812.cpp

C_SOURCES = startup.c \
	../shared/usbd_desc.c \
	$(LIBDAISY_DIR)/Middlewares/ST/STM32_USB_Device_Library/Class/DFU/Src/usbd_dfu.c

C_INCLUDES = -I. -I../shared \
	-I$(LIBDAISY_DIR)/Middlewares/ST/STM32_USB_Device_Library/Class/DFU/Inc

# Sent to the DFU class: our LeaveDFU commits the image instead of a direct reset
C_DEFS = -DUSBD_DFU_VENDOR_EXIT_ENABLED=1 -DUSBD_DFU_APP_DEFAULT_ADD=0x90040000U

# Bare-metal: there is no loader enforcing W^X on ELF program headers
ifneq ($(shell arm-none-eabi-ld --help 2>/dev/null | grep -c no-warn-rwx-segments),0)
LDFLAGS += -Wl,--no-warn-rwx-segments
endif

include $(SYSTEM_FILES_DIR)/Makefile

# startup.c brings in libDaisy's start-up file itself. libdaisy.a stays built for
# STM32H750 (same die and register map); our own files see the real part.
C_SOURCES := $(filter-out %/startup_stm32h750xx.c, $(C_SOURCES))
C_DEFS := $(filter-out -DSTM32H750xx -DSTM32H750IB, $(C_DEFS)) -DSTM32H743xx
