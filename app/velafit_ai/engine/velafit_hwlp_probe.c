/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

#if !defined(CONFIG_BUILD_FLAT) || defined(CONFIG_SMP)
#  error "HWLP isolated probe requires the single-core flat diagnostic build"
#endif

/* Deliberately bounded, interrupt-isolated instruction probe. Never use this
 * critical-section approach around inference; full extension context support
 * is required before using HWLP in a preemptible kernel.
 */

#define CSR_READ(reg, value) \
  __asm__ volatile("csrr %0, " #reg : "=r" (value))
#define CSR_WRITE(reg, value) \
  __asm__ volatile("csrw " #reg ", %0" :: "r" (value) : "memory")

static uint64_t cycle64(void)
{
  uint32_t hi, lo, check;
  do
    {
      __asm__ volatile("rdcycleh %0\nrdcycle %1\nrdcycleh %2"
                       : "=r"(hi), "=r"(lo), "=r"(check));
    }
  while (hi != check);
  return ((uint64_t)hi << 32) | lo;
}

static uint64_t clock_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int velafit_cpu_clock_probe(void)
{
  uint64_t start_us = clock_us();
  uint64_t start_cycles = cycle64();
  uint64_t elapsed;
  do
    {
      elapsed = clock_us() - start_us;
    }
  while (elapsed < 500000);
  uint64_t cycles = cycle64() - start_cycles;
  printf("CPU-CLOCK busy-window cycles=%llu elapsed_us=%llu cycles_per_us=%llu "
         "configured_MHz=%d (scheduler/clock quantization included)\n",
         (unsigned long long)cycles, (unsigned long long)elapsed,
         (unsigned long long)(cycles / elapsed), CONFIG_ESPRESSIF_CPU_FREQ_MHZ);
  return 0;
}

static uint32_t run_loop(uint32_t count)
{
  uint32_t state;
  uint32_t saved[6];
  uint32_t value;
  irqstate_t flags = up_irq_save();

  CSR_READ(0x7f1, state);
  CSR_WRITE(0x7f1, 1u);
  CSR_READ(0x7c6, saved[0]);
  CSR_READ(0x7c7, saved[1]);
  CSR_READ(0x7c8, saved[2]);
  CSR_READ(0x7c9, saved[3]);
  CSR_READ(0x7ca, saved[4]);
  CSR_READ(0x7cb, saved[5]);
  CSR_WRITE(0x7c8, 0u);
  CSR_WRITE(0x7cb, 0u);

  __asm__ volatile(
    ".option push\n"
    ".option norvc\n"
    "li %0, 0\n"
    "la t0, 1f\n"
    "csrw 0x7c6, t0\n"
    "la t0, 2f\n"
    "csrw 0x7c7, t0\n"
    "csrw 0x7c8, %1\n"
    "1: addi %0, %0, 1\n"
    "addi %0, %0, 2\n"
    "2: addi %0, %0, 3\n"
    ".option pop\n"
    : "=&r" (value) : "r" (count) : "t0", "memory");

  CSR_WRITE(0x7c8, 0u);
  CSR_WRITE(0x7c6, saved[0]);
  CSR_WRITE(0x7c7, saved[1]);
  CSR_WRITE(0x7c9, saved[3]);
  CSR_WRITE(0x7ca, saved[4]);
  CSR_WRITE(0x7c8, saved[2]);
  CSR_WRITE(0x7cb, saved[5]);
  CSR_WRITE(0x7f1, state);
  up_irq_restore(flags);
  return value;
}

int velafit_hwlp_probe(void)
{
  static const uint32_t counts[] = {1, 2, 17, 100};
  unsigned int i;
  int errors = 0;

  for (i = 0; i < sizeof(counts) / sizeof(counts[0]); i++)
    {
      uint32_t actual = run_loop(counts[i]);
      uint32_t expected = counts[i] * 6;
      printf("HWLP isolated count=%lu expected=%lu actual=%lu %s\n",
             (unsigned long)counts[i], (unsigned long)expected,
             (unsigned long)actual, actual == expected ? "PASS" : "FAIL");
      errors += actual != expected;
    }

  printf("HWLP instruction-only %s; context-switch and PIE NOT tested\n",
         errors == 0 ? "PASS" : "FAIL");
  return errors == 0 ? 0 : -EIO;
}

#ifdef CONFIG_ESP32P4_HWLP_CONTEXT
extern uint32_t velafit_hwlp_sleep_loop(uint32_t count, uint32_t delta);

struct switch_probe_s
{
  uint32_t count;
  uint32_t delta;
  unsigned int errors;
  unsigned int completed;
};

static void *switch_worker(void *arg)
{
  struct switch_probe_s *probe = arg;
  unsigned int i;

  for (i = 0; i < 10; i++)
    {
      uint32_t value = velafit_hwlp_sleep_loop(probe->count, probe->delta);
      probe->errors += value != probe->count * probe->delta;
      probe->completed++;
    }

  return NULL;
}
#endif

int velafit_hwlp_switch_probe(void)
{
#ifdef CONFIG_ESP32P4_HWLP_CONTEXT
  struct switch_probe_s probes[2] = {{17, 3, 0, 0}, {23, 7, 0, 0}};
  pthread_t threads[2];
  int ret;
  unsigned int i;

  ret = pthread_create(&threads[0], NULL, switch_worker, &probes[0]);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_create(&threads[1], NULL, switch_worker, &probes[1]);
  pthread_join(threads[0], NULL);
  if (ret != 0)
    {
      return -ret;
    }

  pthread_join(threads[1], NULL);
  for (i = 0; i < 2; i++)
    {
      printf("HWLP switch worker=%u rounds=%u count=%lu delta=%lu errors=%u\n",
             i, probes[i].completed, (unsigned long)probes[i].count,
             (unsigned long)probes[i].delta, probes[i].errors);
    }

  ret = probes[0].errors || probes[1].errors ? -EIO : 0;
  printf("HWLP voluntary-switch %s; PIE and nested loops NOT tested\n",
         ret == 0 ? "PASS" : "FAIL");
  return ret;
#else
  return -ENOTSUP;
#endif
}

int velafit_pie_probe(void)
{
#ifdef CONFIG_ESP32P4_PIE_PROBE
  extern int32_t esp32p4_pie_dot16(const int8_t *, const int8_t *);
  int8_t a[32] __attribute__((aligned(16)));
  int8_t b[32] __attribute__((aligned(16)));
  uint32_t state;
  unsigned int offset;
  unsigned int i;
  unsigned int errors = 0;

  for (i = 0; i < 32; i++)
    {
      a[i] = (int8_t)((int)i * 7 - 112);
      b[i] = (int8_t)(95 - (int)i * 5);
    }

  for (offset = 0; offset < 16; offset++)
    {
      int32_t expected = 0;
      int32_t actual;
      irqstate_t flags;
      for (i = 0; i < 16; i++)
        {
          expected += a[offset + i] * b[offset + i];
        }

      flags = up_irq_save();
      CSR_READ(0x7f2, state);
      if ((state & 3) != 0)
        {
          up_irq_restore(flags);
          return -EBUSY;
        }

      actual = esp32p4_pie_dot16(a + offset, b + offset);
      CSR_WRITE(0x7f2, state);
      up_irq_restore(flags);
      printf("PIE isolated offset=%u expected=%ld actual=%ld %s\n", offset,
             (long)expected, (long)actual, actual == expected ? "PASS" : "FAIL");
      errors += actual != expected;
    }

  printf("PIE instruction-only %s; context and inference NOT tested\n",
         errors ? "FAIL" : "PASS");
  return errors ? -EIO : 0;
#else
  return -ENOTSUP;
#endif
}
