OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(EL2_Reset_Handler)

MEMORY
{
    ROM (rx)       : ORIGIN = 0x08000000, LENGTH = 0x800000
    SHARE_RAM (rw) : ORIGIN = 0x10000000, LENGTH = 512K
    APP0_RAM (rw) : ORIGIN = 0x20000000, LENGTH = 64K
    APP1_RAM (rw) : ORIGIN = 0x20010000, LENGTH = 64K
    APP2_RAM (rw) : ORIGIN = 0x20020000, LENGTH = 64K
    SYSTEMAPPLICATION_OSCORE0_RAM (rw) : ORIGIN = 0x20030000, LENGTH = 64K
    SYSTEMAPPLICATION_OSCORE1_RAM (rw) : ORIGIN = 0x20040000, LENGTH = 64K
    RAM (rw)       : ORIGIN = 0x20050000, LENGTH = 0xae000
    SCOPE_RAM (rw) : ORIGIN = 0x200fe000, LENGTH = 0x1000
    RAMx (rw)      : ORIGIN = 0x200FFFC0, LENGTH = 64   /* OsPeripheralArea test */
}

SECTIONS
{
    .text :
    {
        __kernel_rom_start__ = .;
        *(VECTORS)
        *(RESET)

        . = ALIGN(4);
        *(.text)
        *(.text.*)

        /* kernel code section */
        *(.kernel_code)

        /* section information for finsh shell */
        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;

        /* section information for utest */
        . = ALIGN(4);
        __rt_utest_tc_tab_start = .;
        KEEP(*(UtestTcTab))
        __rt_utest_tc_tab_end = .;

        /* section information for initialization */
        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;

        . = ALIGN(4);
        PROVIDE(__ctors_start__ = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        PROVIDE(__ctors_end__ = .);

    } > ROM = 0

    .__kernel_data__ :
    {
        . = ALIGN(64);
        __kernel_ram_start__ = .;
        /* kernel data section */
        __kernel_data_start__ = .;
        *(.kernel_data)
        . = ALIGN(64);
        __kernel_data_end__ = .;
    } > RAM AT > ROM
    __kernel_data_load_start__ = LOADADDR(.__kernel_data__);
    __kernel_rom_end__ = ALIGN(LOADADDR(.__kernel_data__) + SIZEOF(.__kernel_data__), 64);

    . = ALIGN(4);
    .data :
    {
        _sdata = . ;
        KEEP(*(.data))
        KEEP(*(.data.*))

        . = ALIGN(4);
        _edata = . ;
    } > RAM AT > ROM
    _sidata = LOADADDR(.data );

    /* Stabs debugging sections.  */
    .stab 0 : { *(.stab) }
    .stabstr 0 : { *(.stabstr) }
    .stab.excl 0 : { *(.stab.excl) }
    .stab.exclstr 0 : { *(.stab.exclstr) }
    .stab.index 0 : { *(.stab.index) }
    .stab.indexstr 0 : { *(.stab.indexstr) }
    .comment 0 : { *(.comment) }

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _f_stack = .;
        . = . + 24576;
        _e_stack = .;
    } > RAM

    .bss (NOLOAD) :
    {
        . = ALIGN(64);
        __bss_start__ = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;

        . = ALIGN(64);
        __kernel_ram_end__ = .;
    } > RAM

    .ms_scope_workspace (NOLOAD) :
    {
        . = ALIGN(8);
        __ms_scope_workspace_start__ = .;
        KEEP(*(.ms_scope_workspace))
        . = ALIGN(8);
        __ms_scope_workspace_end__ = .;
    } > SCOPE_RAM
    ASSERT(SIZEOF(.ms_scope_workspace) <= LENGTH(SCOPE_RAM),
           "Microscope Scope workspace exceeds reserved RAM")

    __share_code_start__ = ALIGN(LOADADDR(.data) + SIZEOF(.data), 64);
    .share_text :
    {
        . = ALIGN(64);
        KEEP(*(.share_code));
        *(.rodata)                      /* read-only data (constants) */
        *(.rodata*)
    } > ROM

    . = ALIGN(4);
    .ctors :
    {
        PROVIDE(__ctors_start__ = .);
        /* new GCC version uses .init_array */
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__ctors_end__ = .);
    } > ROM

    .dtors :
    {
        PROVIDE(__dtors_start__ = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE(__dtors_end__ = .);
    } > ROM

    .ARM.exidx   :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > ROM

    .SHARE_RAM :
    {
        . = ALIGN(64);
        __share_ram_start__ = .;

        KEEP(*(.share_data))
        KEEP(*(.share_data.*))

        . = ALIGN(64);
        __share_ram_end__ = .;
    } > SHARE_RAM AT > ROM
    __share_ram_load_start__ = LOADADDR(.SHARE_RAM);
    __share_code_end__ = ALIGN(LOADADDR(.SHARE_RAM) + SIZEOF(.SHARE_RAM), 64);

    .thread_private_data :
    {
        KEEP(*(.thread_private_data))
    } > RAM

    .stack_area (NOLOAD) :
    {
        . = ALIGN(8);
        KEEP(*(.stack_area))
    } > RAM

    .__APP_ROM__ :
    {

        . = ALIGN(64);
        __APP0_code_start__ = .;
        KEEP(*(.APP0_CODE));
        . = . - __APP0_code_start__ == 0 ? . + 4 : .;
        . = ALIGN(64);
        __APP0_code_end__ = .;

        . = ALIGN(64);
        __APP1_code_start__ = .;
        KEEP(*(.APP1_CODE));
        . = . - __APP1_code_start__ == 0 ? . + 4 : .;
        . = ALIGN(64);
        __APP1_code_end__ = .;

        . = ALIGN(64);
        __APP2_code_start__ = .;
        KEEP(*(.APP2_CODE));
        . = . - __APP2_code_start__ == 0 ? . + 4 : .;
        . = ALIGN(64);
        __APP2_code_end__ = .;

        . = ALIGN(64);
        __SystemApplication_OsCore0_code_start__ = .;
        KEEP(*(.SystemApplication_OsCore0_CODE));
        . = . - __SystemApplication_OsCore0_code_start__ == 0 ? . + 4 : .;
        . = ALIGN(64);
        __SystemApplication_OsCore0_code_end__ = .;

        . = ALIGN(64);
        __SystemApplication_OsCore1_code_start__ = .;
        KEEP(*(.SystemApplication_OsCore1_CODE));
        . = . - __SystemApplication_OsCore1_code_start__ == 0 ? . + 4 : .;
        . = ALIGN(64);
        __SystemApplication_OsCore1_code_end__ = .;
    } > ROM

    .APP0_RAM :
    {
        . = ALIGN(64);
        __APP0_ram_start__ = .;
        *(.APP0_DATA);
        . = ALIGN(64);
        __APP0_ram_end__ = .;
    } > APP0_RAM AT > ROM
    __APP0_ram_load_start__ = LOADADDR(.APP0_RAM);

    .APP1_RAM :
    {
        . = ALIGN(64);
        __APP1_ram_start__ = .;
        *(.APP1_DATA);
        . = ALIGN(64);
        __APP1_ram_end__ = .;
    } > APP1_RAM AT > ROM
    __APP1_ram_load_start__ = LOADADDR(.APP1_RAM);

    .APP2_RAM :
    {
        . = ALIGN(64);
        __APP2_ram_start__ = .;
        *(.APP2_DATA);
        . = ALIGN(64);
        __APP2_ram_end__ = .;
    } > APP2_RAM AT > ROM
    __APP2_ram_load_start__ = LOADADDR(.APP2_RAM);

    .SYSTEMAPPLICATION_OSCORE0_RAM :
    {
        . = ALIGN(64);
        __SystemApplication_OsCore0_ram_start__ = .;
        *(.SystemApplication_OsCore0_DATA);
        . = ALIGN(64);
        __SystemApplication_OsCore0_ram_end__ = .;
    } > SYSTEMAPPLICATION_OSCORE0_RAM AT > ROM
    __SystemApplication_OsCore0_ram_load_start__ = LOADADDR(.SYSTEMAPPLICATION_OSCORE0_RAM);

    .SYSTEMAPPLICATION_OSCORE1_RAM :
    {
        . = ALIGN(64);
        __SystemApplication_OsCore1_ram_start__ = .;
        *(.SystemApplication_OsCore1_DATA);
        . = ALIGN(64);
        __SystemApplication_OsCore1_ram_end__ = .;
    } > SYSTEMAPPLICATION_OSCORE1_RAM AT > ROM
    __SystemApplication_OsCore1_ram_load_start__ = LOADADDR(.SYSTEMAPPLICATION_OSCORE1_RAM);

    _end = .;
}
