MEMORY
{
  SCOPE_RAM (rw) : ORIGIN = 0x2044E000, LENGTH = 0x2000
}
SECTIONS
{
  .ms_scope_workspace (NOLOAD) :
  {
    . = ALIGN(8);
    __ms_scope_workspace_start__ = .;
    KEEP (*(.ms_scope_workspace*))
    . = ALIGN(8);
    __ms_scope_workspace_end__ = .;
  } > SCOPE_RAM
  ASSERT(SIZEOF(.ms_scope_workspace) <= LENGTH(SCOPE_RAM),
         "Microscope Scope workspace exceeds reserved RAM")
  ASSERT(__shareable_bss_end <= ORIGIN(SCOPE_RAM),
         "S32K344 application RAM overlaps Scope workspace")
}
