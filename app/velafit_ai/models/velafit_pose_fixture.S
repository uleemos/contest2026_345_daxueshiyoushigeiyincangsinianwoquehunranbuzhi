/****************************************************************************
 * app/velafit_ai/models/velafit_pose_fixture.S
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#ifndef VELAFIT_FIXTURE_BUILD_PATH
#define VELAFIT_FIXTURE_BUILD_PATH CONFIG_VELAFIT_POSE_FIXTURE_PATH
#endif

  .section .rodata.velafit_pose_fixture,"a",@progbits
  .balign 16
  .global g_velafit_pose_fixture_rgb192
  .type g_velafit_pose_fixture_rgb192, @object
g_velafit_pose_fixture_rgb192:
  .incbin VELAFIT_FIXTURE_BUILD_PATH
.Lvelafit_pose_fixture_end:
  .size g_velafit_pose_fixture_rgb192, \
        .Lvelafit_pose_fixture_end - g_velafit_pose_fixture_rgb192

  .balign 4
  .global g_velafit_pose_fixture_rgb192_size
  .type g_velafit_pose_fixture_rgb192_size, @object
g_velafit_pose_fixture_rgb192_size:
  .long .Lvelafit_pose_fixture_end - g_velafit_pose_fixture_rgb192
  .size g_velafit_pose_fixture_rgb192_size, 4
