.globl __boot_magic

.data
.align 8
__boot_magic:
  .dword 0

.text
.align 8
.globl _start
_start:
  b reset

.globl reset
reset:
  //why ?
  //mrs x2, S3_1_C15_C3_0 // Read CBAR_EL1 into X2

 // in case someone one day provides us with a cookie
  ldr x8 , __boot_magic
  str x0, [x8]

  b __arch_start
