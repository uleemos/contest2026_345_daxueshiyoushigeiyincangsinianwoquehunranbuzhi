#!/usr/bin/env python3
"""Run actual C request builder/parser with a PC HTTPS transport, fixed data only."""
import argparse
import ctypes as c
import datetime
import json
from pathlib import Path
import time
import urllib.request
import urllib.error
from test_cloud_agent import CloudTest

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--env',type=Path,required=True)
    args=parser.parse_args()
    values={}
    for line in args.env.read_text().splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            k,v=line.split('=',1); values[k.strip()]=v.strip().strip('"').strip("'")
    key=values.get('MIMO_API_KEY','')
    if not key.startswith('sk-') or '\n' in key or '\r' in key:
        raise SystemExit('ordinary API key missing or invalid')
    CloudTest.setUpClass()
    try:
        def send(ctx,body,output,capacity):
            try:
                request=urllib.request.Request('https://api.xiaomimimo.com/v1/chat/completions',
                    data=body,headers={'api-key':key,'Content-Type':'application/json'},method='POST')
                start=time.monotonic()
                with urllib.request.urlopen(request,timeout=60) as response:
                    result=json.loads(response.read(16384))
                    content=result['choices'][0]['message']['content']
                    print(json.dumps(dict(execution='PC',time=datetime.datetime.now().astimezone().isoformat(),
                        model=result.get('model'),status=response.status,elapsed_ms=round(1000*(time.monotonic()-start)),
                        content=content.replace(key,'<redacted>')),ensure_ascii=False),flush=True)
                raw=content.encode()+b'\0'
                if len(raw)>capacity: return -75
                c.memmove(output,raw,len(raw)); return 0
            except urllib.error.HTTPError as error:
                detail=error.read(2048).decode('utf-8','replace').replace(key,'<redacted>')
                print('PC transport HTTP status='+str(error.code)+' body='+detail,flush=True)
                return -5
            except Exception as error:
                print('PC transport error type='+type(error).__name__,flush=True)
                return -5
        out=c.create_string_buffer(2048)
        ret=CloudTest.lib.velafit_cloud_agent_submit_workout_via(
            b'{"session_id":"VF-FIXED-001","exercise":"squat","reps":10,"source":"fixed-test-summary"}',
            out,CloudTest.callback(send),None)
        print('PC C schema result='+str(ret))
        return 0 if ret==0 else 1
    finally:
        CloudTest.doClassCleanups()

if __name__=='__main__': raise SystemExit(main())
