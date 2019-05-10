.extern __boot_magic

.text
.align 8
.globl _start
_start:
  // in case someone one day provides us with a cookie
  ldr x8 , =__boot_magic
  str x0, [x8]
  b reset

.globl reset
reset:


  b __arch_start
