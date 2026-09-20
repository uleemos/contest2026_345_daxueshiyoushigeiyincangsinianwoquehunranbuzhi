/* SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "velafit_body_check.h"

static const int g_body_points[8] =
{
  KPT_LEFT_SHOULDER, KPT_RIGHT_SHOULDER,
  KPT_LEFT_HIP, KPT_RIGHT_HIP,
  KPT_LEFT_KNEE, KPT_RIGHT_KNEE,
  KPT_LEFT_ANKLE, KPT_RIGHT_ANKLE
};

static velafit_body_center_t center(const pose_frame_t *pose, int a, int b)
{
  velafit_body_center_t value =
  {
    (pose->kpts[a].x + pose->kpts[b].x) * 0.5f,
    (pose->kpts[a].y + pose->kpts[b].y) * 0.5f
  };
  return value;
}

void velafit_body_check_default_config(velafit_body_check_config_t *config)
{
  if (!config) return;
  config->confidence_threshold = 0.30f;
  /* INT8 confidence changes in roughly 0.0034 steps.  Require every point to
   * clear a hard floor, while allowing at most two otherwise-valid points to
   * sit below the primary threshold.  Empty scenes remain below the floor. */
  config->confidence_floor = 0.20f;
  config->minimum_strong_points = 6;
  config->frame_margin = 0.04f;
  /* Board calibration: the accepted standing set was 0.383--0.400 while
   * the deliberately distant set was 0.366--0.372 in model coordinates.
   */
  config->minimum_body_height = 0.375f;
  /* Portrait-v3b board calibration: normal accepted standing was about
   * 0.38--0.50, while the nearest complete-body set began at 0.560.
   * Keep this configurable; 0.54 leaves a small dead-band between them. */
  config->maximum_body_height = 0.54f;
  config->minimum_vertical_ratio = 1.50f;
  config->minimum_segment_gap = 0.015f;
  config->maximum_pair_vertical_delta = 0.15f;
  config->maximum_knee_vertical_delta = 0.10f;
  config->stable_frames_required = 5;
}

void velafit_body_check_init(velafit_body_check_t *check,
                             const velafit_body_check_config_t *config)
{
  if (!check) return;
  if (config)
    {
      check->config = *config;
    }
  else
    {
      velafit_body_check_default_config(&check->config);
    }
  if (check->config.stable_frames_required == 0)
    {
      check->config.stable_frames_required = 1;
    }
  check->stable_frames = 0;
}

static velafit_body_check_status_t fail(velafit_body_check_t *check,
  velafit_body_check_result_t *result, velafit_body_check_status_t status)
{
  check->stable_frames = 0;
  result->stable_frames = 0;
  result->status = status;
  return status;
}

velafit_body_check_status_t velafit_body_check_update(
  velafit_body_check_t *check, const pose_frame_t *pose,
  velafit_body_check_result_t *result)
{
  float min_x = 1.0f;
  float min_y = 1.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
  if (!check || !result) return BODY_CHECK_LOW_CONFIDENCE;
  memset(result, 0, sizeof(*result));
  result->orientation = BODY_ORIENTATION_LOW_CONFIDENCE;

  if (!pose)
    {
      return fail(check, result, BODY_CHECK_LOW_CONFIDENCE);
    }

  for (unsigned int i = 0; i < 8; i++)
    {
      const kpt_2d_t *point = &pose->kpts[g_body_points[i]];
      if (isfinite(point->x) && isfinite(point->y) &&
          isfinite(point->score) &&
          point->score >= check->config.confidence_floor)
        {
          result->confident_points++;
          if (point->score >= check->config.confidence_threshold)
            {
              result->strong_points++;
            }
          if (point->x < min_x) min_x = point->x;
          if (point->x > max_x) max_x = point->x;
          if (point->y < min_y) min_y = point->y;
          if (point->y > max_y) max_y = point->y;
        }
    }
  if (result->confident_points != 8 ||
      result->strong_points < check->config.minimum_strong_points)
    {
      bool upper_body_visible = true;
      for (unsigned int i = 0; i < 4; i++)
        {
          const kpt_2d_t *point = &pose->kpts[g_body_points[i]];
          upper_body_visible = upper_body_visible && isfinite(point->score) &&
            point->score >= check->config.confidence_threshold;
        }
      /* A confident shoulder/hip torso with missing knees or ankles is a
       * cropped person, not a generic recognition failure. */
      if (upper_body_visible)
        {
          return fail(check, result, BODY_CHECK_NOT_FULLY_VISIBLE);
        }
      return fail(check, result, BODY_CHECK_LOW_CONFIDENCE);
    }

  result->shoulder_center = center(pose, KPT_LEFT_SHOULDER,
                                  KPT_RIGHT_SHOULDER);
  result->hip_center = center(pose, KPT_LEFT_HIP, KPT_RIGHT_HIP);
  result->knee_center = center(pose, KPT_LEFT_KNEE, KPT_RIGHT_KNEE);
  result->ankle_center = center(pose, KPT_LEFT_ANKLE, KPT_RIGHT_ANKLE);
  result->body_dx = result->ankle_center.x - result->shoulder_center.x;
  result->body_dy = result->ankle_center.y - result->shoulder_center.y;
  result->body_height = hypotf(result->body_dx, result->body_dy);
  result->bbox_width = max_x - min_x;
  result->bbox_height = max_y - min_y;

  bool ordered =
    result->hip_center.y - result->shoulder_center.y >=
      check->config.minimum_segment_gap &&
    result->knee_center.y - result->hip_center.y >=
      check->config.minimum_segment_gap &&
    result->ankle_center.y - result->knee_center.y >=
      check->config.minimum_segment_gap;
  bool vertical = fabsf(result->body_dy) >=
    fabsf(result->body_dx) * check->config.minimum_vertical_ratio;
  result->orientation = ordered && vertical ? BODY_ORIENTATION_UPRIGHT :
                        BODY_ORIENTATION_ROTATED_OR_INVALID;
  if (result->orientation != BODY_ORIENTATION_UPRIGHT)
    {
      return fail(check, result, BODY_CHECK_ORIENTATION_INVALID);
    }

  for (unsigned int level = 0; level < 4; level++)
    {
      const kpt_2d_t *left = &pose->kpts[g_body_points[level * 2]];
      const kpt_2d_t *right = &pose->kpts[g_body_points[level * 2 + 1]];
      const float maximum_delta = level == 2 ?
        check->config.maximum_knee_vertical_delta :
        check->config.maximum_pair_vertical_delta;
      if (fabsf(left->y - right->y) > maximum_delta)
        {
          return fail(check, result, BODY_CHECK_NOT_FULLY_VISIBLE);
        }
    }

  const float margin = check->config.frame_margin;
  for (unsigned int i = 0; i < 8; i++)
    {
      const kpt_2d_t *point = &pose->kpts[g_body_points[i]];
      if (point->x < margin || point->x > 1.0f - margin ||
          point->y < margin || point->y > 1.0f - margin)
        {
          return fail(check, result, BODY_CHECK_NOT_FULLY_VISIBLE);
        }
    }

  if (result->body_height > check->config.maximum_body_height)
    {
      return fail(check, result, BODY_CHECK_TOO_CLOSE);
    }
  if (result->body_height < check->config.minimum_body_height)
    {
      return fail(check, result, BODY_CHECK_TOO_FAR);
    }

  if (check->stable_frames < UINT16_MAX) check->stable_frames++;
  result->stable_frames = check->stable_frames;
  result->status = check->stable_frames >=
                   check->config.stable_frames_required ?
                   BODY_CHECK_READY : BODY_CHECK_STABILIZING;
  return result->status;
}

const char *velafit_body_check_status_name(velafit_body_check_status_t status)
{
  static const char *names[] =
  {
    "BODY_CHECK_LOW_CONFIDENCE", "BODY_CHECK_NOT_FULLY_VISIBLE",
    "BODY_CHECK_TOO_CLOSE", "BODY_CHECK_TOO_FAR",
    "BODY_CHECK_ORIENTATION_INVALID", "BODY_CHECK_STABILIZING",
    "BODY_CHECK_READY"
  };
  return (unsigned int)status < sizeof(names) / sizeof(names[0]) ?
         names[status] : "BODY_CHECK_UNKNOWN";
}

const char *velafit_body_orientation_name(velafit_body_orientation_t value)
{
  static const char *names[] =
  {
    "LOW_CONFIDENCE", "UPRIGHT", "ROTATED_OR_INVALID"
  };
  return (unsigned int)value < sizeof(names) / sizeof(names[0]) ?
         names[value] : "ROTATED_OR_INVALID";
}

const char *velafit_body_check_user_message(velafit_body_check_status_t status)
{
  switch (status)
    {
      case BODY_CHECK_TOO_CLOSE: return "MOVE BACK";
      case BODY_CHECK_TOO_FAR: return "MOVE CLOSER";
      case BODY_CHECK_READY: return "STAND READY";
      case BODY_CHECK_STABILIZING: return "HOLD STILL";
      case BODY_CHECK_ORIENTATION_INVALID: return "STEP INTO FRAME";
      case BODY_CHECK_LOW_CONFIDENCE:
      case BODY_CHECK_NOT_FULLY_VISIBLE:
      default: return "STEP INTO FRAME";
    }
}
