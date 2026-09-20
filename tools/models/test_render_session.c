/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "velafit_render.h"

int main(void)
{
  const unsigned sizes[][2]={{320,240},{600,1024},{1024,600},{16,80},{8,8}};
  for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++)
    {
      unsigned w=sizes[i][0],h=sizes[i][1];
      size_t bytes=w*h*2;
      uint8_t *memory=malloc(bytes+128);
      assert(memory);
      memset(memory,0xa5,bytes+128);
      velafit_canvas_t canvas;
      velafit_canvas_init(&canvas,memory+64,w,h,VELAFIT_PIXFMT_RGB565);
      velafit_canvas_clear(&canvas,VELAFIT_COLOR_BLACK);
      for (unsigned state=0;state<5;state++)
        {
          velafit_render_session_status(&canvas,"RUNNING",UINT32_MAX,
                                         state&1,state&2);
          for (unsigned j=0;j<64;j++)
            assert(memory[j]==0xa5 && memory[bytes+64+j]==0xa5);
        }
      /* Drawing the same state is deterministic and does not accumulate. */
      uint8_t *snapshot=malloc(bytes);
      assert(snapshot);
      memcpy(snapshot,memory+64,bytes);
      velafit_render_session_status(&canvas,"RUNNING",UINT32_MAX,false,false);
      assert(!memcmp(snapshot,memory+64,bytes));
      free(snapshot); free(memory);
    }
  /* Check actual rendered pixels, not just transform arithmetic. */
  uint16_t *pixels=calloc(1024*600,2);
  assert(pixels);
  velafit_canvas_t camera;
  velafit_canvas_init(&camera,(uint8_t *)pixels,1024,600,VELAFIT_PIXFMT_RGB565);
  pose_frame_t model={0}; model.valid=true;
  /* View center of crop = (360,639.5); model x=.5, y=639.5/1280. */
  model.kpts[KPT_NOSE].x=.5f;
  model.kpts[KPT_NOSE].y=639.5f/1280;
  model.kpts[KPT_NOSE].score=1;
  velafit_render_sc2336_skeleton(&camera,&model,1280,720,0);
  assert(pixels[300*1024+512]==0xffff);
  velafit_canvas_clear(&camera,VELAFIT_COLOR_BLACK);
  model.kpts[KPT_NOSE].y=0; /* Cropped upper body point must not stick to border. */
  velafit_render_sc2336_skeleton(&camera,&model,1280,720,0);
  for(unsigned i=0;i<1024*600;i++) assert(pixels[i]==0);
  model.kpts[KPT_NOSE].y=NAN;
  velafit_render_sc2336_skeleton(&camera,&model,1280,720,0);
  for(unsigned i=0;i<1024*600;i++) assert(pixels[i]==0);
  free(pixels);
  puts("HOST status renderer: bounds/guard/determinism PASS; no LCD acceptance");
  return 0;
}
