/* SPDX-License-Identifier: Apache-2.0 */
#include <pthread.h>
/* Upstream scratch pointer is process-global. All local users serialize. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
int velafit_esp_nn_lock(void) { return pthread_mutex_lock(&g_lock); }
void velafit_esp_nn_unlock(void) { pthread_mutex_unlock(&g_lock); }
