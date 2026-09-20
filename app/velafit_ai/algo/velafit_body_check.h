/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_BODY_CHECK_H
#define VELAFIT_BODY_CHECK_H

#include <stdint.h>
#include "velafit_types.h"

typedef enum
{
  BODY_CHECK_LOW_CONFIDENCE = 0,
  BODY_CHECK_NOT_FULLY_VISIBLE,
  BODY_CHECK_TOO_CLOSE,
  BODY_CHECK_TOO_FAR,
  BODY_CHECK_ORIENTATION_INVALID,
  BODY_CHECK_STABILIZING,
  BODY_CHECK_READY
} velafit_body_check_status_t;

typedef enum
{
  BODY_ORIENTATION_LOW_CONFIDENCE = 0,
  BODY_ORIENTATION_UPRIGHT,
  BODY_ORIENTATION_ROTATED_OR_INVALID
} velafit_body_orientation_t;

typedef struct
{
  float confidence_threshold;
  float confidence_floor;
  uint8_t minimum_strong_points;
  float frame_margin;
  float minimum_body_height;
  float maximum_body_height;
  float minimum_vertical_ratio;
  float minimum_segment_gap;
  float maximum_pair_vertical_delta;
  float maximum_knee_vertical_delta;
  uint16_t stable_frames_required;
} velafit_body_check_config_t;

typedef struct
{
  float x;
  float y;
} velafit_body_center_t;

typedef struct
{
  velafit_body_check_status_t status;
  velafit_body_orientation_t orientation;
  velafit_body_center_t shoulder_center;
  velafit_body_center_t hip_center;
  velafit_body_center_t knee_center;
  velafit_body_center_t ankle_center;
  float body_dx;
  float body_dy;
  float body_height;
  float bbox_width;
  float bbox_height;
  uint16_t stable_frames;
  uint8_t confident_points;
  uint8_t strong_points;
} velafit_body_check_result_t;

typedef struct
{
  velafit_body_check_config_t config;
  uint16_t stable_frames;
} velafit_body_check_t;

#ifdef __cplusplus
extern "C"
{
#endif

void velafit_body_check_default_config(velafit_body_check_config_t *config);
void velafit_body_check_init(velafit_body_check_t *check,
                             const velafit_body_check_config_t *config);
velafit_body_check_status_t velafit_body_check_update(
  velafit_body_check_t *check, const pose_frame_t *pose,
  velafit_body_check_result_t *result);
const char *velafit_body_check_status_name(velafit_body_check_status_t status);
const char *velafit_body_orientation_name(velafit_body_orientation_t value);
const char *velafit_body_check_user_message(velafit_body_check_status_t status);

#ifdef __cplusplus
}
#endif
#endif
