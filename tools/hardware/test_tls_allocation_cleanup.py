#!/usr/bin/env python3
"""Compile the actual request function with HOST fault-injection stubs.

Only allocation/cleanup/error-timing paths are tested; no TLS or network claim.
"""
from pathlib import Path
import subprocess
import tempfile

TEAM = Path(__file__).resolve().parents[2]
source = (TEAM/'app/c6_wifi/c6_mimo_tls.c').read_text()
start = source.index('static int c6_mimo_attempt(')
end = source.index('\n/****************************************************************************', start)
function = source[start:end]
prefix = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "c6_http_policy.h"
#define FAR
#define OK 0
#define MIMO_HOST "example.invalid"
#define MIMO_PATH "/unused"
#define MIMO_REQUEST_MAX 2048
#define MIMO_RESPONSE_MAX 32768
struct c6_tls_s {int ssl; int initialized;};
struct c6_mimo_result_s {int http_status; unsigned long elapsed_ms;};
static int allocation, fail_at, alive, tls_frees, connects;
static void *mock_malloc(size_t n) {
  if (++allocation == fail_at) return NULL;
  void *p = malloc(n); assert(p); alive++; return p;
}
static void *mock_calloc(size_t n, size_t size) {
  void *p = mock_malloc(n*size); if(p) memset(p,0,n*size); return p;
}
static void mock_free(void *p) {if(p) {alive--; free(p);}}
static int c6_tls_connect(struct c6_tls_s *tls, const char *host) {
  (void)host; connects++; tls->initialized=1; return -EIO;
}
static void c6_tls_free(struct c6_tls_s *tls) {
  assert(tls->initialized == 1); tls_frees++;
}
static void c6_secure_zero(void *p, size_t n) {memset(p,0,n);}
static unsigned long c6_elapsed(const struct timespec *t) {(void)t; return 123;}
static int c6_tls_write_all(int *p,const unsigned char *b,size_t n) {return -EIO;}
static int c6_tls_read_response(int *p,char *b,size_t n,size_t *s) {return -EIO;}
static int c6_http_parse(char *b,size_t n,int *s,char **o,size_t *z) {return -EIO;}
static int c6_mimo_parse_json(char *b,struct c6_mimo_result_s *r) {return -EIO;}
#define malloc mock_malloc
#define calloc mock_calloc
#define free mock_free
'''
suffix = r'''
int main(void) {
  for(int fault=1; fault<=4; fault++) {
    allocation=alive=tls_frees=connects=0; fail_at=fault;
    struct c6_mimo_result_s result={0};
    int ret=c6_mimo_attempt("not-a-real-key", "{}", 2, &result);
    assert(ret == (fault<=3 ? -ENOMEM : -EIO));
    assert(alive == 0);
    assert(tls_frees == (fault==4));
    assert(connects == (fault==4));
    assert(result.elapsed_ms == 123);
  }
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='velafit-tls-cleanup-') as tmp:
    binary = str(Path(tmp)/'cleanup')
    subprocess.run(['cc', '-x', 'c', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-fsanitize=undefined',
                    '-I'+str(TEAM/'app/c6_wifi'), '-', '-o', binary],
                   input=prefix+function+suffix, text=True, check=True)
    subprocess.run([binary], check=True, timeout=5)
print('HOST actual request function: allocation failures1/2/3 and connect failure PASS')
print('Mock TLS/clock only; no network, hardware or real allocator exhaustion claim')
