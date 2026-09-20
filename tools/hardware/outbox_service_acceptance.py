#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""P4 actual SD + interface down/up + real MiMo. Silent, fixed summaries only.

Run phase=store on fresh firmware/boot, then reboot and phase=recover.
The fixed IDs must be new for store acceptance. No deletion/format/overwrite.
"""
import argparse
import datetime
from pathlib import Path
import re
import time
from c6_mimo_device_acceptance import load_env, open_port, read_until, send, command, emit

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',default='COM3')
    p.add_argument('--env',type=Path,required=True)
    p.add_argument('--log',type=Path,required=True)
    p.add_argument('--phase',choices=('store','recover'),required=True)
    p.add_argument('--id',default='VF-AUTO-20260915-01')
    args=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,31}',args.id): p.error('invalid ID')
    env=load_env(args.env)
    key=env.get('MIMO_API_KEY',''); password=env.get('VELAFIT_WIFI_PASSWORD','')
    ssid=env.get('VELAFIT_WIFI_SSID','')
    if not key.startswith('sk-') or not password or not ssid: p.error('missing ordinary key/Wi-Fi config')
    secrets=(password.encode(),key.encode())
    port=open_port(args.port,20)
    try:
        with args.log.open('wb') as log:
            emit(f'P4 outbox service phase={args.phase} started={datetime.datetime.now().astimezone().isoformat()} fixed_id={args.id}\n'.encode(),log)
            send(port,''); read_until(port,b'nsh>',15,log,secrets)
            send(port,'c6_wifi connect')
            read_until(port,b'Wi-Fi SSID:',25,log,secrets); send(port,ssid)
            read_until(port,b'Wi-Fi password (hidden):',10,log,secrets); send(port,password)
            linked=read_until(port,b'nsh>',45,log,secrets)
            if b'netdev=eth0' not in linked: raise RuntimeError('association failed')
            command(port,'ifup eth0',10,log,secrets)
            command(port,'ifconfig eth0 dhcp',30,log,secrets)
            send(port,'c6_wifi queue-start')
            read_until(port,b'MiMo API key (hidden):',45,log,secrets); send(port,key)
            ready=read_until(port,b'OUTBOX scheduler ready:',120,log,secrets)
            if b'OUTBOX scheduler stopped' in ready: raise RuntimeError('scheduler startup failed')

            def status_until(predicate, seconds=90):
                end=time.monotonic()+seconds
                while time.monotonic()<end:
                    result=command(port,'c6_wifi queue-status',10,log,secrets)
                    matches=re.findall(rb'OUTBOX status ([^\r\n]+)',result)
                    if matches:
                        fields=dict((k.decode(),int(v)) for k,v in
                                    re.findall(rb'(\w+)=(-?\d+)',matches[-1]))
                        if any(fields.get(k,0) for k in ('service_error','rejected','corrupt','io_errors','storage_failures','error')):
                            raise RuntimeError('scheduler error/rejected record')
                        if predicate(fields): return fields
                    time.sleep(1)
                raise TimeoutError('scheduler status condition not reached')

            if args.phase=='store':
                command(port,'ifdown eth0',10,log,secrets)
                status_until(lambda s:s.get('online')==0,15)
                result=command(port,f'c6_wifi queue-add {args.id}',10,log,secrets)
                if b'OUTBOX enqueue ret=0 receipt=RAM-only' not in result:
                    raise RuntimeError('enqueue rejected')
                status_until(lambda s:s.get('ram')==0 and s.get('persisted')==1 and s.get('sent')==0)
                emit(b'CHECK actual interface-down SD persistence: PASS; cloud NOT invoked for this phase\n',log)
            else:
                status_until(lambda s:s.get('sent')==1 and s.get('ram')==0)
                # Duplicate same fixed payload must not trigger another send.
                result=command(port,f'c6_wifi queue-add {args.id}',10,log,secrets)
                if b'OUTBOX enqueue ret=0' not in result: raise RuntimeError('duplicate enqueue failed')
                status_until(lambda s:s.get('persisted')==1 and s.get('ram')==0 and s.get('sent')==1)
                chain=command(port,'velafit_ai mww_chain',20,log,secrets)
                if b'MWW CHAIN result=0' not in chain or b'SDMMCPROBE: timeout' in chain or b'SDMMCPROBE: wait error' in chain:
                    raise RuntimeError('KWS/storage parallel regression failed')
                tls=command(port,'c6_wifi tls',60,log,secrets)
                if b'TLS verify PASS: host=api.xiaomimimo.com' not in tls:
                    raise RuntimeError('C6/TLS unusable after parallel KWS')
                status_until(lambda s:s.get('sent')==1 and s.get('ram')==0)
                emit(b'CHECK reboot pending recovery + real sender ACK/dedup: PASS\n',log)
            command(port,'c6_wifi queue-stop',10,log,secrets)
            status_until(lambda s:s.get('state')==4 and s.get('ram')==0)
            if args.phase=='recover':
                ack=command(port,f'c6_wifi sd-outbox-ack {args.id}',90,log,secrets)
                if f'OUTBOX stored ACK id={args.id} result=1 '.encode() not in ack:
                    raise RuntimeError('explicit persisted ACK recheck failed')
            command(port,'free',10,log,secrets)
            command(port,'ps',10,log,secrets)
            emit(b'CHECK scheduler clean stop: PASS\n',log)
    finally:
        port.close()
        key=password=''
    return 0

if __name__=='__main__': raise SystemExit(main())
