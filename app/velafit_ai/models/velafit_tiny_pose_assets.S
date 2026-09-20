/****************************************************************************
 * app/velafit_ai/models/velafit_tiny_pose_assets.S
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

  .section .rodata.velafit_tiny_pose,"a",@progbits
  .balign 16
  .global g_velafit_tiny_pose_model
  .type g_velafit_tiny_pose_model, @object
g_velafit_tiny_pose_model:
  .incbin VELAFIT_TINY_MODEL_PATH
.Ltiny_pose_model_end:
  .size g_velafit_tiny_pose_model, \
        .Ltiny_pose_model_end - g_velafit_tiny_pose_model

  .balign 4
  .global g_velafit_tiny_pose_model_size
  .type g_velafit_tiny_pose_model_size, @object
g_velafit_tiny_pose_model_size:
  .long .Ltiny_pose_model_end - g_velafit_tiny_pose_model
  .size g_velafit_tiny_pose_model_size, 4

  .balign 16
  .global g_velafit_tiny_pose_fixture
  .type g_velafit_tiny_pose_fixture, @object
g_velafit_tiny_pose_fixture:
  .incbin VELAFIT_TINY_FIXTURE_PATH
.Ltiny_pose_fixture_end:
  .size g_velafit_tiny_pose_fixture, \
        .Ltiny_pose_fixture_end - g_velafit_tiny_pose_fixture

  .balign 4
  .global g_velafit_tiny_pose_fixture_size
  .type g_velafit_tiny_pose_fixture_size, @object
g_velafit_tiny_pose_fixture_size:
  .long .Ltiny_pose_fixture_end - g_velafit_tiny_pose_fixture
  .size g_velafit_tiny_pose_fixture_size, 4

  .balign 16
  .global g_velafit_tiny_pose_oracle
  .type g_velafit_tiny_pose_oracle, @object
g_velafit_tiny_pose_oracle:
  .incbin VELAFIT_TINY_ORACLE_PATH
.Ltiny_pose_oracle_end:
  .size g_velafit_tiny_pose_oracle, \
        .Ltiny_pose_oracle_end - g_velafit_tiny_pose_oracle

  .balign 4
  .global g_velafit_tiny_pose_oracle_size
  .type g_velafit_tiny_pose_oracle_size, @object
g_velafit_tiny_pose_oracle_size:
  .long .Ltiny_pose_oracle_end - g_velafit_tiny_pose_oracle
  .size g_velafit_tiny_pose_oracle_size, 4
