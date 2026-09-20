/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#ifdef CONFIG_ESP32P4_HWLP_CONTEXT
  .text
  .align 2
  .global velafit_hwlp_sleep_loop
  .type velafit_hwlp_sleep_loop, @function
/* count, increment -> sum. Voluntary blocking inside a live hardware loop
 * forces real scheduler context switches. Call only from diagnostic tasks.
 */
velafit_hwlp_sleep_loop:
  addi sp, sp, -16
  sw ra, 12(sp)
  sw s0, 8(sp)
  sw s1, 4(sp)
  li s0, 0
  mv s1, a1
  csrwi 0x7f1, 1
  csrwi 0x7c8, 0
  csrwi 0x7cb, 0
  la t0, .Lloop_start
  csrw 0x7c6, t0
  la t0, .Lloop_end
  csrw 0x7c7, t0
  csrw 0x7c8, a0
.Lloop_start:
  add s0, s0, s1
  li a0, 1000
  call usleep
.Lloop_end:
  nop
  csrwi 0x7c8, 0
  csrwi 0x7f1, 0
  mv a0, s0
  lw s1, 4(sp)
  lw s0, 8(sp)
  lw ra, 12(sp)
  addi sp, sp, 16
  ret
  .size velafit_hwlp_sleep_loop, . - velafit_hwlp_sleep_loop
#endif
