/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sc2336_model_preview.h"

int main(void)
{
  uint8_t image[96 * 96 * 3];
  uint16_t framebuffer[240 * 220];
  memset(image, 0, sizeof(image));
  memset(framebuffer, 0xff, sizeof(framebuffer));

  /* Source top-left red -> rotated bottom-left; source top-right green ->
   * rotated top-left. Scale=2 must duplicate each exact model pixel 2x2. */
  image[0] = 255;
  image[(95 * 3) + 1] = 255;
  assert(sc2336_model_preview_blit_ccw_rgb565(image, sizeof(image),
    framebuffer, sizeof(framebuffer), 220, 220, 240, 2) == 0);
  assert(framebuffer[14 * 240 + 14] == 0x07e0);
  assert(framebuffer[204 * 240 + 14] == 0xf800);
  assert(framebuffer[205 * 240 + 15] == 0xf800);
  assert(framebuffer[0] == 0);
  assert(sc2336_model_preview_blit_ccw_rgb565(image, sizeof(image),
    framebuffer, sizeof(framebuffer), 220, 220, 240, 0) < 0);
  puts("MODEL_INPUT preview CCW90/center/integer-scale PASS");
  return 0;
}
