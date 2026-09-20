/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_QUEUE_SERVICE_H
#define C6_QUEUE_SERVICE_H
int c6_queue_start(const char *key);
int c6_queue_stop(void);
int c6_queue_status(void);
int c6_queue_submit(const char *id, const char *summary);
#endif
