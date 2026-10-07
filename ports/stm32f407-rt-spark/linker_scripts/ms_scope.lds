MEMORY
{
    SCOPE_RAM (rw) : ORIGIN = 0x2001E000, LENGTH = 0x2000
}

SECTIONS
{
    .ms_scope_workspace (NOLOAD) :
    {
        . = ALIGN(512);
        __ms_scope_workspace_start__ = .;
        KEEP(*(.ms_scope_workspace*))
        . = ALIGN(8);
        __ms_scope_workspace_end__ = .;
    } > SCOPE_RAM
    ASSERT(SIZEOF(.ms_scope_workspace) <= LENGTH(SCOPE_RAM),
           "Microscope Scope workspace exceeds reserved RAM")
}
INSERT AFTER .STACK_AREA;
