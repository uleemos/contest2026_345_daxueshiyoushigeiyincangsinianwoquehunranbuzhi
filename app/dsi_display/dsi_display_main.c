/****************************************************************************
 * app/dsi_display/dsi_display_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <nuttx/video/fb.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FB_DEV_PATH "/dev/fb0"

#define RGB565(r, g, b) \
  ((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | (((b) & 0xf8) >> 3)))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dsi_show_usage
 ****************************************************************************/

static void dsi_show_usage(FAR const char *progname)
{
  printf("Usage: %s <command> [args...]\n", progname);
  printf("Commands:\n");
  printf("  info                Show framebuffer and resolution info\n");
  printf("  bars                Draw 8-color vertical test pattern bars\n");
  printf("  color <r> <g> <b>   Fill screen with RGB color (0..255)\n");
  printf("  grid                Draw alignment grid and border lines\n");
  printf("  fps [sec]           Benchmark rendering FPS (default: 3 sec)\n");
}

/****************************************************************************
 * Name: dsi_set_pixel
 ****************************************************************************/

static void dsi_set_pixel(FAR uint8_t *fb, uint32_t stride, uint8_t bpp,
                          uint32_t x, uint32_t y,
                          uint8_t r, uint8_t g, uint8_t b)
{
  if (bpp == 16)
    {
      FAR uint16_t *row = (FAR uint16_t *)(fb + y * stride);
      row[x] = RGB565(r, g, b);
    }
  else if (bpp == 24)
    {
      FAR uint8_t *pixel = fb + y * stride + x * 3;
      pixel[0] = r;
      pixel[1] = g;
      pixel[2] = b;
    }
  else if (bpp == 32)
    {
      FAR uint32_t *row = (FAR uint32_t *)(fb + y * stride);
      row[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

/****************************************************************************
 * Name: dsi_fill_color
 ****************************************************************************/

static void dsi_fill_color(FAR uint8_t *fb, uint32_t width, uint32_t height,
                           uint32_t stride, uint8_t bpp,
                           uint8_t r, uint8_t g, uint8_t b)
{
  uint32_t x;
  uint32_t y;

  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        {
          dsi_set_pixel(fb, stride, bpp, x, y, r, g, b);
        }
    }
}

/****************************************************************************
 * Name: dsi_draw_bars
 ****************************************************************************/

static void dsi_draw_bars(FAR uint8_t *fb, uint32_t width, uint32_t height,
                          uint32_t stride, uint8_t bpp)
{
  static const uint8_t colors[8][3] =
  {
    {255, 255, 255}, /* White */
    {255, 255,   0}, /* Yellow */
    {  0, 255, 255}, /* Cyan */
    {  0, 255,   0}, /* Green */
    {255,   0, 255}, /* Magenta */
    {255,   0,   0}, /* Red */
    {  0,   0, 255}, /* Blue */
    {  0,   0,   0}, /* Black */
  };

  uint32_t bar_width = width / 8;
  uint32_t x;
  uint32_t y;
  int c;

  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        {
          c = x / bar_width;
          if (c > 7)
            {
              c = 7;
            }

          dsi_set_pixel(fb, stride, bpp, x, y,
                        colors[c][0], colors[c][1], colors[c][2]);
        }
    }
}

/****************************************************************************
 * Name: dsi_draw_grid
 ****************************************************************************/

static void dsi_draw_grid(FAR uint8_t *fb, uint32_t width, uint32_t height,
                          uint32_t stride, uint8_t bpp)
{
  uint32_t x;
  uint32_t y;

  /* 1. Fill Dark Grey Background */

  dsi_fill_color(fb, width, height, stride, bpp, 32, 32, 32);

  /* 2. Draw 50px Grid Lines */

  for (x = 0; x < width; x += 50)
    {
      for (y = 0; y < height; y++)
        {
          dsi_set_pixel(fb, stride, bpp, x, y, 64, 64, 64);
        }
    }

  for (y = 0; y < height; y += 50)
    {
      for (x = 0; x < width; x++)
        {
          dsi_set_pixel(fb, stride, bpp, x, y, 64, 64, 64);
        }
    }

  /* 3. Draw Outer Border (White) */

  for (x = 0; x < width; x++)
    {
      dsi_set_pixel(fb, stride, bpp, x, 0, 255, 255, 255);
      dsi_set_pixel(fb, stride, bpp, x, height - 1, 255, 255, 255);
    }

  for (y = 0; y < height; y++)
    {
      dsi_set_pixel(fb, stride, bpp, 0, y, 255, 255, 255);
      dsi_set_pixel(fb, stride, bpp, width - 1, y, 255, 255, 255);
    }

  /* 4. Draw Center Crosshairs (Cyan) */

  for (x = 0; x < width; x++)
    {
      dsi_set_pixel(fb, stride, bpp, x, height / 2, 0, 255, 255);
    }

  for (y = 0; y < height; y++)
    {
      dsi_set_pixel(fb, stride, bpp, width / 2, y, 0, 255, 255);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dsi_display_main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  FAR uint8_t *fb;
  int fd;
  int ret;

  if (argc < 2)
    {
      dsi_show_usage(argv[0]);
      return 1;
    }

  fd = open(FB_DEV_PATH, O_RDWR);
  if (fd < 0)
    {
      fprintf(stderr, "ERROR: Cannot open %s: %d\n", FB_DEV_PATH, errno);
      return 1;
    }

  ret = ioctl(fd, FBIOGET_VIDEOINFO, (unsigned long)&vinfo);
  if (ret < 0)
    {
      fprintf(stderr, "ERROR: FBIOGET_VIDEOINFO failed: %d\n", errno);
      close(fd);
      return 1;
    }

  ret = ioctl(fd, FBIOGET_PLANEINFO, (unsigned long)&pinfo);
  if (ret < 0 || pinfo.fbmem == NULL)
    {
      fprintf(stderr, "ERROR: FBIOGET_PLANEINFO failed: %d\n", errno);
      close(fd);
      return 1;
    }

  fb = (FAR uint8_t *)pinfo.fbmem;

  if (strcmp(argv[1], "info") == 0)
    {
      printf("=== ESP32-P4 MIPI DSI Framebuffer Info ===\n");
      printf("Resolution : %" PRIu32 " x %" PRIu32 "\n",
             vinfo.xres, vinfo.yres);
      printf("Color Format: %d (BPP: %u)\n", vinfo.fmt, pinfo.bpp);
      printf("Stride     : %" PRIu32 " bytes\n", (uint32_t)pinfo.stride);
      printf("Buffer Size: %zu bytes\n", pinfo.fblen);
      printf("Memory Addr: %p\n", pinfo.fbmem);
    }
  else if (strcmp(argv[1], "bars") == 0)
    {
      printf("Drawing 8-color test bars on display...\n");
      dsi_draw_bars(fb, vinfo.xres, vinfo.yres, pinfo.stride, pinfo.bpp);
      ioctl(fd, FBIOPAN_DISPLAY, (unsigned long)&pinfo);
      printf("Color bars rendered successfully [OK]\n");
    }
  else if (strcmp(argv[1], "color") == 0)
    {
      uint8_t r = 255;
      uint8_t g = 255;
      uint8_t b = 255;

      if (argc >= 5)
        {
          r = (uint8_t)atoi(argv[2]);
          g = (uint8_t)atoi(argv[3]);
          b = (uint8_t)atoi(argv[4]);
        }

      printf("Filling display with RGB(%u, %u, %u)...\n", r, g, b);
      dsi_fill_color(fb, vinfo.xres, vinfo.yres, pinfo.stride, pinfo.bpp,
                     r, g, b);
      ioctl(fd, FBIOPAN_DISPLAY, (unsigned long)&pinfo);
      printf("Fill completed [OK]\n");
    }
  else if (strcmp(argv[1], "grid") == 0)
    {
      printf("Drawing alignment grid and border lines...\n");
      dsi_draw_grid(fb, vinfo.xres, vinfo.yres, pinfo.stride, pinfo.bpp);
      ioctl(fd, FBIOPAN_DISPLAY, (unsigned long)&pinfo);
      printf("Grid rendered [OK]\n");
    }
  else if (strcmp(argv[1], "fps") == 0)
    {
      int duration = 3;
      time_t start;
      time_t now;
      uint32_t frames = 0;
      uint32_t bx = 100;
      uint32_t by = 100;
      int dx = 5;
      int dy = 5;
      uint32_t x;
      uint32_t y;

      if (argc >= 3)
        {
          duration = atoi(argv[2]);
          if (duration <= 0)
            {
              duration = 3;
            }
        }

      printf("Starting FPS animation benchmark for %d seconds...\n",
             duration);
      start = time(NULL);

      while ((time(NULL) - start) < duration)
        {
          /* Clear screen to black */

          dsi_fill_color(fb, vinfo.xres, vinfo.yres,
                         pinfo.stride, pinfo.bpp, 0, 0, 0);

          /* Draw moving 100x100 box */

          for (y = by; y < by + 100 && y < vinfo.yres; y++)
            {
              for (x = bx; x < bx + 100 && x < vinfo.xres; x++)
                {
                  dsi_set_pixel(fb, pinfo.stride, pinfo.bpp,
                                x, y, 0, 255, 0);
                }
            }

          bx += dx;
          by += dy;

          if (bx + 100 >= vinfo.xres || bx <= 0)
            {
              dx = -dx;
            }

          if (by + 100 >= vinfo.yres || by <= 0)
            {
              dy = -dy;
            }

          ioctl(fd, FBIOPAN_DISPLAY, (unsigned long)&pinfo);
          frames++;
        }

      now = time(NULL);
      if (now > start)
        {
          printf("Benchmark results: %" PRIu32 " frames in %ld sec "
                 "(%.1f FPS)\n",
                 frames, (long)(now - start),
                 (float)frames / (float)(now - start));
        }
    }
  else
    {
      dsi_show_usage(argv[0]);
    }

  close(fd);
  return 0;
}
