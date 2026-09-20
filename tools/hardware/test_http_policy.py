#!/usr/bin/env python3
"""HOST C policy regression. No requests, credentials or board access."""
from pathlib import Path
import subprocess
import tempfile

TEAM = Path(__file__).resolve().parents[2]
SOURCE = r'''
#include <assert.h>
#include "c6_http_policy.h"
int main(void) {
  assert(c6_http_result_error(200) == 0);
  assert(c6_http_result_error(299) == 0);
  assert(c6_http_result_error(401) == -EACCES);
  assert(c6_http_result_error(403) == -EACCES);
  assert(c6_http_result_error(400) == -EINVAL);
  assert(c6_http_result_error(404) == -EINVAL);
  assert(c6_http_result_error(422) == -EINVAL);
  assert(c6_http_result_error(408) == -ETIMEDOUT);
  assert(c6_http_result_error(504) == -ETIMEDOUT);
  assert(c6_http_result_error(425) == -EAGAIN);
  assert(c6_http_result_error(429) == -EAGAIN);
  assert(c6_http_result_error(500) == -EREMOTEIO);
  assert(c6_http_result_error(503) == -EREMOTEIO);
  assert(c6_http_result_error(302) == -EBADMSG);
  assert(c6_http_result_error(0) == -EBADMSG);
  assert(c6_http_result_error(600) == -EBADMSG);
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='velafit-http-policy-') as tmp:
    binary = str(Path(tmp) / 'policy')
    subprocess.run(['cc', '-x', 'c', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-I'+str(TEAM/'app/c6_wifi'),
                    '-', '-o', binary], input=SOURCE, text=True, check=True)
    subprocess.run([binary], check=True, timeout=5)
print('HOST HTTP policy: 16 status cases PASS; no network/hardware validation')
