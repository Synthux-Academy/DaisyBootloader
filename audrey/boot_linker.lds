/* Audrey bootloader, STM32H743VIT6. See ../specs/audrey-bootloader.md.
 *
 * Only .boot executes from flash: the reset handler, the boot flow and the copy
 * loop. Everything else runs from AXI SRAM, because code fetched from bank 1 would
 * stall while bank 1 is erased or programmed, stopping USB and the LEDs.
 */

ENTRY(Reset_Handler)

MEMORY
{
	FLASH       (RX)  : ORIGIN = 0x08000000, LENGTH = 128K
	DTCMRAM     (RWX) : ORIGIN = 0x20000000, LENGTH = 128K
	SRAM        (RWX) : ORIGIN = 0x24000000, LENGTH = 512K
	RAM_D2_DMA  (RWX) : ORIGIN = 0x30000000, LENGTH = 32K
	RAM_D2      (RWX) : ORIGIN = 0x30008000, LENGTH = 256K
	RAM_D3      (RWX) : ORIGIN = 0x38000000, LENGTH = 64K
	BACKUP_SRAM (RWX) : ORIGIN = 0x38800000, LENGTH = 4K
}

/* Same as the app: below the boot request word (0x2001FFF0), and within the ROM
 * loader's stack limit (RAM end - 16 bytes, AN2606 Table 124) */
_estack = 0x2001FFE0;

SECTIONS
{
	.boot :
	{
		KEEP(*(.boot_vectors))
		*(.boot_text*)
		. = ALIGN(4);
	} > FLASH

	/* One block: start-up copies it from flash as a whole. The vector table needs
	 * 1 KB alignment for VTOR (166 entries). */
	.ram_image :
	{
		. = ALIGN(1024);
		_sram_image = .;
		KEEP(*(.isr_vector))
		. = ALIGN(4);
		*(.text)
		*(.text*)
		*(.rodata)
		*(.rodata*)
		*(.glue_7)
		*(.glue_7t)
		KEEP(*(.init))
		KEEP(*(.fini))
		. = ALIGN(4);
		_etext = .;

		*(.ARM.extab)
		*(.gnu.linkonce.armextab.*)
		. = ALIGN(4);
		PROVIDE(__exidx_start = .);
		*(.ARM.exidx*)
		PROVIDE(__exidx_end = .);
		. = ALIGN(4);

		PROVIDE_HIDDEN(__preinit_array_start = .);
		KEEP(*(.preinit_array*))
		PROVIDE_HIDDEN(__preinit_array_end = .);
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array*))
		PROVIDE_HIDDEN(__init_array_end = .);
		PROVIDE_HIDDEN(__fini_array_start = .);
		KEEP(*(.fini_array*))
		KEEP(*(SORT(.fini_array.*)))
		PROVIDE_HIDDEN(__fini_array_end = .);

		*(.data)
		*(.data*)
		. = ALIGN(4);
		_eram_image = .;
	} > SRAM AT > FLASH

	_siram_image = LOADADDR(.ram_image);

	/* libDaisy's Reset_Handler copies .data itself. It's part of the image above,
	 * so give that loop nothing to do. */
	_sidata = _eram_image;
	_sdata  = _eram_image;
	_edata  = _eram_image;

	.ARM.attributes 0 : { *(.ARM.attributes) }

	.bss (NOLOAD) :
	{
		. = ALIGN(4);
		_sbss = .;
		*(.bss)
		*(.bss*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;
	} > SRAM

	PROVIDE(end = .);

	.dtcmram_bss (NOLOAD) :
	{
		. = ALIGN(4);
		*(.dtcmram_bss)
		*(.dtcmram_bss*)
		. = ALIGN(4);
	} > DTCMRAM

	.sram1_bss (NOLOAD) :
	{
		. = ALIGN(4);
		*(.sram1_bss)
		*(.sram1_bss*)
		. = ALIGN(4);
	} > RAM_D2_DMA

	.backup_sram (NOLOAD) :
	{
		. = ALIGN(4);
		*(.backup_sram)
		*(.backup_sram*)
		. = ALIGN(4);
	} > BACKUP_SRAM

	/* Nothing may land in SDRAM or QSPI: neither exists on this board */
	/DISCARD/ :
	{
		*(.sdram_bss*)
		*(.qspiflash_text*)
		*(.qspiflash_data*)
		*(.qspiflash_bss*)
	}
}
