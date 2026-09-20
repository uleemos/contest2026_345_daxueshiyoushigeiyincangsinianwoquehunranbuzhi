/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#ifdef CONFIG_ESP32P4_FPU_DIAGNOSTIC
struct fpu_case { unsigned int id; unsigned int errors; };

static void *fpu_worker(void *arg)
{
  struct fpu_case *test = arg;
  for (unsigned int i = 0; i < 20; i++)
    {
      const uint32_t pattern = 0x3f000000u + test->id * 0x100000u + i;
      const uint32_t flags = test->id << 5; /* RNE vs RTZ */
      uint32_t actual, actual_flags, start, elapsed;
      /* Hold a register over multiple timer interrupts without any function
       * calls (ilp32 makes all FPRs caller-clobbered). Two runnable RR tasks
       * use distinct patterns and rounding modes. 12M cycles = 30ms at400MHz.
       */
      __asm__ volatile(
        "fmv.w.x f31, %[value]\nfscsr %[flags]\ncsrr %[start], 0xb00\n"
        "1: csrr %[elapsed], 0xb00\nsub %[elapsed], %[elapsed], %[start]\n"
        "bltu %[elapsed], %[duration], 1b\n"
        "fmv.x.w %[actual], f31\nfrcsr %[actual_flags]\nfscsr zero\n"
        : [actual] "=&r"(actual), [actual_flags] "=&r"(actual_flags),
          [start] "=&r"(start), [elapsed] "=&r"(elapsed)
        : [value] "r"(pattern), [flags] "r"(flags), [duration] "r"(12000000u)
        : "f31", "memory");
      if (actual != pattern || actual_flags != flags) test->errors++;
    }
  return NULL;
}
#endif

int velafit_fpu_probe(void)
{
#ifdef CONFIG_ESP32P4_FPU_DIAGNOSTIC
  pthread_t threads[2];
  struct fpu_case cases[2] = {{0, 0}, {1, 0}};
  int ret = pthread_create(&threads[0], NULL, fpu_worker, &cases[0]);
  if (ret) return -ret;
  ret = pthread_create(&threads[1], NULL, fpu_worker, &cases[1]);
  pthread_join(threads[0], NULL);
  if (ret) return -ret;
  pthread_join(threads[1], NULL);
  printf("FPU-CONTEXT threads=2 rounds=20 errors=%u/%u\n",
         cases[0].errors, cases[1].errors);
  return cases[0].errors || cases[1].errors ? -EIO : 0;
#else
  return -ENOTSUP;
#endif
}
