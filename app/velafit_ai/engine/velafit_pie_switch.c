/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef CONFIG_ESP32P4_PIE_CONTEXT
extern void esp32p4_pie_save_regs(void *);
extern void esp32p4_pie_load_regs(const void *);
extern void esp32p4_pie_unaligned_seed(const void *);

struct pie_probe_s
{
  unsigned int id;
  unsigned int completed;
  unsigned int seed_errors;
  unsigned int switch_errors;
  unsigned int first_byte;
};

static void *pie_worker(void *arg)
{
  struct pie_probe_s *p = arg;
  uint8_t seed[224] __attribute__((aligned(16)));
  uint8_t expected[224] __attribute__((aligned(16)));
  uint8_t actual[224] __attribute__((aligned(16)));
  unsigned int round;
  unsigned int i;

  for (round = 0; round < 25; round++)
    {
      memset(seed, 0, sizeof(seed));
      memset(expected, 0, sizeof(expected));
      memset(actual, 0, sizeof(actual));
      for (i = 0; i < 192; i++)
        {
          seed[i] = (uint8_t)(i * 7 + round * 11 + p->id * 83);
        }

      for (i = 208; i < 213; i++)
        {
          seed[i] = (uint8_t)(i + round + p->id * 19);
        }

      seed[213] = 0x35; /* SAR_BYTES=3, FFT width=5 */
      seed[214] = 7;
      seed[216] = 2; /* ESP-NN misaligned-load CFG, tracking disabled */
      __asm__ volatile("csrwi 0x7f2, 1" ::: "memory");
      esp32p4_pie_load_regs(seed);
      esp32p4_pie_save_regs(expected);
      p->seed_errors += memcmp(seed, expected, 192) != 0;
      esp32p4_pie_unaligned_seed(seed + 1);
      esp32p4_pie_save_regs(expected);
      usleep(1000);
      esp32p4_pie_save_regs(actual);
      __asm__ volatile("csrwi 0x7f2, 0" ::: "memory");
      for (i = 0; i < 220; i++)
        {
          if (actual[i] != expected[i])
            {
              if (p->switch_errors == 0)
                {
                  p->first_byte = i;
                }

              p->switch_errors++;
              break;
            }
        }

      p->completed++;
    }

  return NULL;
}
#endif

int velafit_pie_switch_probe(void)
{
#ifdef CONFIG_ESP32P4_PIE_CONTEXT
  struct pie_probe_s probes[2] = {{0}, {1}};
  pthread_t threads[2];
  pthread_attr_t attr;
  unsigned int i;
  unsigned int started = 0;
  int ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_attr_setstacksize(&attr, 8192);
  for (i = 0; ret == 0 && i < 2; i++)
    {
      ret = pthread_create(&threads[i], &attr, pie_worker, &probes[i]);
      if (ret == 0)
        {
          started++;
        }
    }

  pthread_attr_destroy(&attr);
  for (i = 0; i < started; i++)
    {
      int joined = pthread_join(threads[i], NULL);
      if (joined != 0 && ret == 0)
        {
          ret = joined;
        }

      printf("PIE switch worker=%u rounds=%u seed_errors=%u "
             "switch_errors=%u first_byte=%u\n", i, probes[i].completed,
             probes[i].seed_errors, probes[i].switch_errors,
             probes[i].first_byte);
      if (probes[i].completed != 25 || probes[i].seed_errors ||
          probes[i].switch_errors)
        {
          ret = EIO;
        }
    }

  printf("PIE voluntary-switch %s; CFG=2 task-only, no inference acceptance\n",
         ret == 0 ? "PASS" : "FAIL");
  return -ret;
#else
  return -ENOTSUP;
#endif
}
