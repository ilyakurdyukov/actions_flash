// https://support.elvees.com/docs/Instructions/assembly-project-development/

OUTPUT_ARCH("mips:isa32")
TARGET("elf32-littlemips")
ENTRY(_start)
SEARCH_DIR(.)
SECTIONS {

 . =0xB8001000;
 .text :
  {
     * (.text);
     * (.init)
     * (.rel.sdata)
     * (.fini)
  }

 .data :
  {
     * (.data);
  }

  _gp = ALIGN(16);
  .lit4 : { *(.lit4) }
  .sdata : { *(.sdata) }
  .rodata : { *(rodata) }
  . = ALIGN(8);
  PROVIDE (edata = .);
  _edata = .;

  _fbss = .;
  .sbss : { *(.sbss*) *(.scommon) *(*ABS*)}

  .bss  :
  {
     _bss_start = .;
     *(.bss);
     *(COMMON)
  }

  . += 0x0000800;
  PROVIDE(__stack = ALIGN(8));
  . += 0x10;


  PROVIDE(end = .);
  _end = .;
}
