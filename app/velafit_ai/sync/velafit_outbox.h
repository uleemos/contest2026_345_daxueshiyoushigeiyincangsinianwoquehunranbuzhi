/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_OUTBOX_H
#define VELAFIT_OUTBOX_H
#include <stdint.h>
/* Single-owner queue: call only from its scheduler thread. Directory must
 * already exist on caller-verified durable storage. No fallback to /tmp.
 * ACKs remain for local dedup; unknown remote outcomes are at-least-once.
 */
typedef int (*vf_outbox_sender)(void *ctx, const char *summary);
struct vf_outbox_scan
{
  unsigned corrupt_records;
  unsigned visited_records;
  unsigned io_errors;
};
int vf_outbox_enqueue(const char *directory, const char *id, const char *summary);
/* 1 only for an explicit valid persisted ACK; 0 pending, negative on error.
 * Independent of wall clock, retry deadline and network availability. */
int vf_outbox_is_acked(const char *directory, const char *id);
/* Preserves corrupt entries but continues to independent valid records.
 * A successful send can coexist with corrupt_records>0. Single scheduler owner.
 */
int vf_outbox_tick_report(const char *directory, int64_t now,
                         vf_outbox_sender sender, void *ctx,
                         struct vf_outbox_scan *scan);
int vf_outbox_tick(const char *directory, int64_t now,
                   vf_outbox_sender sender, void *ctx);
#endif
