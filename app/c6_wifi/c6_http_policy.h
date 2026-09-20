/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_HTTP_POLICY_H
#define C6_HTTP_POLICY_H
#include <errno.h>

/* Match outbox permanent-error classification; credentials and malformed
 * requests must not be retried ten times. Never follow redirects with keys.
 */
static inline int c6_http_result_error(int status)
{
  if (status >= 200 && status < 300) return 0;
  if (status == 401 || status == 403) return -EACCES;
  if (status == 408 || status == 504) return -ETIMEDOUT;
  if (status == 425 || status == 429) return -EAGAIN;
  if (status >= 400 && status < 500) return -EINVAL;
  if (status >= 500 && status < 600) return -EREMOTEIO;
  return -EBADMSG;
}
#endif
