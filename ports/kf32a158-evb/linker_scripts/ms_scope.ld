MEMORY
{
  SCOPE_RAM (rw) : ORIGIN = 0x1002E000, LENGTH = 0x2000
}

SECTIONS
{
  .ms_scope_workspace (NOLOAD) :
  {
    . = ALIGN(512);
    __ms_scope_workspace_start__ = .;
    KEEP (*(.ms_scope_workspace*))
    . = ALIGN(8);
    __ms_scope_workspace_end__ = .;
  } > SCOPE_RAM
  ASSERT(SIZEOF(.ms_scope_workspace) <= LENGTH(SCOPE_RAM),
         "Microscope Scope workspace exceeds reserved RAM")
  ASSERT(stack_area_end <= ORIGIN(SCOPE_RAM),
         "KF32A158 application RAM overlaps Scope workspace")
}
