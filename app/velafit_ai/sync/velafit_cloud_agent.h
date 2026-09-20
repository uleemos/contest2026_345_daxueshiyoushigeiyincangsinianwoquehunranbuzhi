/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/sync/velafit_cloud_agent.h
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

#ifndef __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_SYNC_VELAFIT_CLOUD_AGENT_H
#define __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_SYNC_VELAFIT_CLOUD_AGENT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../include/velafit_mimo_protocol.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

int velafit_cloud_agent_init(void);

/* Synchronous transport; no retained key/context and no implicit mock. */
typedef int (*velafit_cloud_transport_t)(void *ctx, const char *request,
                                        char *content, size_t capacity);
int velafit_cloud_agent_submit_workout_via(
      const char *session_json, velafit_mimo_prescription_t *out_presc,
      velafit_cloud_transport_t transport, void *ctx);

void velafit_cloud_agent_deinit(void);

int velafit_cloud_agent_submit_workout(
      const char *session_json,
      velafit_mimo_prescription_t *out_presc);

int velafit_cloud_agent_submit_voice(
      const uint8_t *pcm_data,
      size_t len,
      velafit_mimo_asr_result_t *out_asr);

int velafit_cloud_agent_run_simulation(void);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_SYNC_VELAFIT_CLOUD_AGENT_H */
