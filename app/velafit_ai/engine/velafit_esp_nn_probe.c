/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef VELAFIT_ESP_NN_TEST
#ifndef CONFIG_ESP32P4_PIE_CONTEXT
#error "ESP-NN test requires validated PIE/HWLP task context"
#endif
#include <esp_nn_riscv_pie.h>
#ifdef VELAFIT_QACC_CONV
#include "velafit_qacc_conv.h"
#endif
extern int velafit_esp_nn_lock(void);
extern void velafit_esp_nn_unlock(void);

static uint64_t probe_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int conv_case(int channels, int kernel, int padding, int stride, int depthwise)
{
  data_dims_t input = {9, 8, channels, 1};
  data_dims_t filter = {kernel, kernel, channels, 1};
  data_dims_t output = {(9 + 2 * padding - kernel) / stride + 1,
                        (8 + 2 * padding - kernel) / stride + 1, depthwise ? channels : 7, 1};
#ifdef VELAFIT_QACC_CONV
  if (!depthwise) output.channels = channels + 1;
#endif
  conv_params_t params = {3, -7, {stride, stride}, {padding, padding},
                          {1, 1}, {-128, 127}};
  dw_conv_params_t dwparams = {3, -7, 1, {stride, stride}, {padding, padding},
                              {1, 1}, {-128, 127}};
  int32_t bias[960];
  int32_t shifts[960];
  int32_t mult[960];
  quant_data_t quant = {shifts, mult};
  size_t insize = input.width * input.height * channels;
  size_t filtersize = kernel * kernel * channels * (depthwise ? 1 : output.channels);
  size_t outsize = output.width * output.height * output.channels;
  int scratch_size = depthwise ? 0 : esp_nn_get_conv_scratch_size_riscv_pie(
      &input, &filter, &output, &params);
#ifdef VELAFIT_ESP_NN_V132
  if (depthwise) scratch_size = esp_nn_get_depthwise_conv_scratch_size_riscv_pie(
      &input, &filter, &output, &dwparams);
#endif
#ifdef VELAFIT_QACC_CONV
  const int elements = kernel * kernel * channels;
  const int padded = (elements + 15) & ~15;
  int8_t *packed = NULL;
  int32_t *combined = NULL;
  if (!depthwise) scratch_size = padded;
#endif
  uint8_t *scratch = NULL;
  int8_t *in = NULL;
  int8_t *weights = NULL;
  int8_t *reference = NULL;
  int8_t *actual = NULL;
  unsigned int errors = 0;
  unsigned int guards = 0;
  size_t i;
  uint64_t start;
  uint64_t ansi_us;
  uint64_t pie_us;
  int ret = -ENOMEM;

  if (scratch_size < 0 || (!depthwise && scratch_size == 0) || scratch_size > 1024 * 1024)
    {
      return -EOVERFLOW;
    }

  /* Diagnostic quarantine: upstream small-window tiled sizing is suspect.
   * Keep the reported-boundary canary, but absorb writes inside a dedicated
   * 64 KiB allocation. Never use this as an unchecked production workaround.
   */
  size_t quarantine = scratch_size < 65536 ? 65536 : scratch_size;
  scratch = memalign(16, quarantine + 32);
  in = memalign(16, insize + 32);
  weights = memalign(16, filtersize + 32);
  reference = malloc(outsize);
  actual = memalign(16, outsize + 32);
  if (!scratch || !in || !weights || !reference || !actual)
    {
      goto out;
    }

  memset(scratch, 0xa5, quarantine + 32);
  memset(actual, 0xa5, outsize + 32);
  memset(in, 0, insize + 32);
  memset(weights, 0, filtersize + 32);
  for (i = 0; i < insize; i++) in[i] = (int)(i * 17 % 251) - 125;
  for (i = 0; i < filtersize; i++) weights[i] = (int)(i * 7 % 25) - 12;
  for (i = 0; i < (size_t)output.channels; i++)
    {
      bias[i] = (int)i * 51 - 139;
      shifts[i] = -3 - (int)(i % 3);
      mult[i] = 1073741824 + i * 100003;
    }
#ifdef VELAFIT_QACC_CONV
  if (!depthwise)
    {
      packed = memalign(16, padded * ((output.channels + 15) & ~15));
      combined = malloc(output.channels * sizeof(int32_t));
      if (!packed || !combined) goto out;
      vf_qacc_pack(weights, bias, packed, combined, elements, output.channels, params.in_offset);
    }
#endif

  start = probe_us();
  if (depthwise)
    esp_nn_depthwise_conv_s8_ansi(&input, in, &filter, weights, bias, &output,
                                 reference, &dwparams, &quant);
  else
    esp_nn_conv_s8_ansi(&input, in, &filter, weights, bias, &output,
                       reference, &params, &quant);
  ansi_us = probe_us() - start;
  __asm__ volatile("csrwi 0x7f1, 1" ::: "memory");
  start = probe_us();
  if (depthwise)
    {
      esp_nn_set_depthwise_conv_scratch_buf_riscv_pie(scratch);
      esp_nn_depthwise_conv_s8_riscv_pie(&input, in, &filter, weights, bias, &output,
                                      actual, &dwparams, &quant);
      esp_nn_set_depthwise_conv_scratch_buf_riscv_pie(NULL);
    }
  else
    {
      esp_nn_set_conv_scratch_buf_riscv_pie(scratch);
#ifdef VELAFIT_QACC_CONV
      vf_qacc_conv(&input, in, &filter, packed, combined, &output, actual,
                    &params, &quant, (int8_t *)scratch);
#else
      esp_nn_conv_s8_riscv_pie(&input, in, &filter, weights, bias, &output,
                              actual, &params, &quant);
#endif
    }
  pie_us = probe_us() - start;
  esp_nn_set_conv_scratch_buf_riscv_pie(NULL);
  __asm__ volatile("csrwi 0x7f2, 0\ncsrwi 0x7f1, 0" ::: "memory");
  for (i = 0; i < outsize; i++) errors += actual[i] != reference[i];
  for (i = 0; i < 32; i++)
    {
      guards += (uint8_t)actual[outsize + i] != 0xa5;
      guards += scratch[scratch_size + i] != 0xa5;
      guards += scratch[quarantine + i] != 0xa5;
    }

  printf("ESP-NN %s ch=%d k=%d pad=%d stride=%d outputs=%u "
         "mismatch=%u guard_errors=%u ansi_us=%llu pie_us=%llu scratch=%d\n",
         depthwise ? "depthwise" : "conv", channels, kernel, padding, stride, (unsigned int)outsize, errors,
         guards, (unsigned long long)ansi_us, (unsigned long long)pie_us,
         scratch_size);
  ret = errors || guards ? -EIO : 0;
out:
#ifdef VELAFIT_QACC_CONV
  free(packed);
  free(combined);
#endif
  free(scratch);
  free(in);
  free(weights);
  free(reference);
  free(actual);
  return ret;
}
#endif

int velafit_esp_nn_probe(void)
{
#ifdef VELAFIT_ESP_NN_TEST
  static const int channels[] = {3, 8, 16, 24, 32};
  unsigned int i;
  unsigned int passed = 0;
  unsigned int failed = 0;
  int kernel;
  int padding;
  int stride;
  if (velafit_esp_nn_lock() != 0) return -EBUSY;
  for (i = 0; i < sizeof(channels) / sizeof(channels[0]); i++)
    for (kernel = 1; kernel <= 3; kernel += 2)
      for (padding = 0; padding <= 1; padding++)
        for (stride = 1; stride <= 2; stride++)
          {
            int ret = conv_case(channels[i], kernel, padding, stride, 0);
            if (ret == 0) passed++;
            else failed++;
          }

  printf("ESP-NN upstream conv reference cases pass=%u fail=%u; "
         "no model acceptance, timings limited by OS clock resolution\n",
         passed, failed);
  unsigned int dwpassed = 0;
  unsigned int dwfailed = 0;
  for (i = 0; i < sizeof(channels) / sizeof(channels[0]); i++)
    for (kernel = 3; kernel <= 5; kernel += 2)
      for (padding = 0; padding <= 1; padding++)
        for (stride = 1; stride <= 2; stride++)
          {
            int ret = conv_case(channels[i], kernel, padding, stride, 1);
            if (ret == 0) dwpassed++;
            else dwfailed++;
          }
  printf("ESP-NN upstream depthwise reference cases pass=%u fail=%u\n", dwpassed, dwfailed);
  // Dedicated large-channel boundary cases, separate from original40.
  int large_channels[] = {256, 320, 960};
  for (i = 0; i < 3; i++)
    {
      int ret = conv_case(large_channels[i], 3, 1, 1, 1);
      printf("ESP-NN depthwise boundary ch=%d result=%d\n", large_channels[i], ret);
      dwfailed += ret != 0;
    }
  velafit_esp_nn_unlock();
  return failed || dwfailed ? -EIO : 0;
#else
  return -ENOTSUP;
#endif
}
