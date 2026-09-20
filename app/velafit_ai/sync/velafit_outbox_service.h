/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_OUTBOX_SERVICE_H
#define VELAFIT_OUTBOX_SERVICE_H
#include <stdbool.h>
#include "velafit_outbox.h"
struct vf_outbox_service;
struct vf_outbox_service_stats
{
  unsigned pending_ram, persisted, sent, rejected, storage_failures;
  unsigned corrupt_records;
  unsigned io_errors;
  int last_error;
  bool online;
};
struct vf_outbox_service *vf_outbox_service_create(const char *directory,
                                                   vf_outbox_sender sender,
                                                   void *context);
/* Nonblocking RAM acceptance only, NOT a durable receipt. Capacity four. */
int vf_outbox_service_enqueue(struct vf_outbox_service *s, const char *id,
                              const char *summary);
int vf_outbox_service_status(struct vf_outbox_service *s,
                             struct vf_outbox_service_stats *out);
int vf_outbox_service_online(struct vf_outbox_service *s, bool online);
/* Exactly one scheduler owns step; sender runs without holding UI/mailbox
 * lock. No sender invocation while explicitly offline. Storage always runs.
 */
int vf_outbox_service_step(struct vf_outbox_service *s, int64_t now);
/* Owner only, after step returned and all clients stopped. Refuses RAM loss. */
int vf_outbox_service_destroy(struct vf_outbox_service *s);
#endif
