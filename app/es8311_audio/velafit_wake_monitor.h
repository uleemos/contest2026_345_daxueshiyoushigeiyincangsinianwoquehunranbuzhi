/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_WAKE_MONITOR_H
#define VELAFIT_WAKE_MONITOR_H

/* Wait for one real microphone "nihao openvela" match.  Capture stops as
 * soon as the accepted event is observed or max_seconds expires. */
int velafit_wake_wait_once(unsigned int max_seconds);

#endif
