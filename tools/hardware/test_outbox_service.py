#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""HOST only: actual queue persistence, explicit FAKE slow/failing sender."""
import ctypes as c
import errno
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest

class Stats(c.Structure):
    _fields_ = [(name, c.c_uint) for name in
                ('pending_ram','persisted','sent','rejected','storage_failures','corrupt_records','io_errors')]
    _fields_ += [('last_error',c.c_int),('online',c.c_bool)]

class ServiceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='vf-service-build-')
        cls.addClassCleanup(cls.build.cleanup)
        team=Path(__file__).resolve().parents[2]
        binary=Path(cls.build.name)/'service.so'
        sources=[team/'app/velafit_ai/sync'/p for p in
                 ('velafit_outbox.c','velafit_outbox_service.c')]
        sources += [team.parent/'apps/netutils/cjson/cJSON/cJSON.c']
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-pthread',
                        '-shared','-fPIC','-I'+str(team.parent/'apps/include'),
                        *map(str,sources),'-lm','-o',str(binary)],check=True)
        cls.lib=c.CDLL(str(binary))
        cls.Callback=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_char_p)
        cls.lib.vf_outbox_service_create.argtypes=[c.c_char_p,cls.Callback,c.c_void_p]
        cls.lib.vf_outbox_service_create.restype=c.c_void_p
        cls.lib.vf_outbox_service_enqueue.argtypes=[c.c_void_p,c.c_char_p,c.c_char_p]
        cls.lib.vf_outbox_service_step.argtypes=[c.c_void_p,c.c_int64]
        cls.lib.vf_outbox_service_online.argtypes=[c.c_void_p,c.c_bool]
        cls.lib.vf_outbox_service_status.argtypes=[c.c_void_p,c.POINTER(Stats)]
        cls.lib.vf_outbox_service_destroy.argtypes=[c.c_void_p]

    def setUp(self):
        self.data=tempfile.TemporaryDirectory(prefix='vf-service-data-')
        self.addCleanup(self.data.cleanup)
        self.path=Path(self.data.name)/'queue'
        self.path.mkdir()
        self.calls=[]; self.result=0; self.block=False
        self.enter=threading.Event(); self.release=threading.Event()
        def send(ctx, summary):
            self.calls.append(json.loads(summary))
            self.enter.set()
            if self.block and not self.release.wait(3): return -errno.ETIMEDOUT
            return self.result
        self.callback=self.Callback(send)
        self.service=self.lib.vf_outbox_service_create(str(self.path).encode(),self.callback,None)
        self.assertTrue(self.service)

    def add(self, number):
        id=f'session-{number}'
        body=json.dumps(dict(session_id=id,exercise='squat',reps=3)).encode()
        return self.lib.vf_outbox_service_enqueue(self.service,id.encode(),body)

    def stats(self):
        out=Stats()
        self.assertEqual(self.lib.vf_outbox_service_status(self.service,c.byref(out)),0)
        return out

    def step(self, now=100):
        return self.lib.vf_outbox_service_step(self.service,now)

    def tearDown(self):
        self.release.set()
        self.lib.vf_outbox_service_online(self.service,False)
        if not self.path.exists(): self.path.mkdir()
        for _ in range(4): self.step(2000000000)
        self.assertEqual(self.lib.vf_outbox_service_destroy(self.service),0)

    def test_offline_persists_then_online_sends(self):
        for n in range(4): self.assertEqual(self.add(n),0)
        self.assertEqual(self.add(4),-errno.EAGAIN)
        for _ in range(4): self.assertEqual(self.step(),0)
        self.assertEqual(self.calls,[])
        self.assertEqual(self.stats().persisted,4)
        self.lib.vf_outbox_service_online(self.service,True)
        for _ in range(4): self.assertEqual(self.step(),1)
        self.assertEqual(self.step(),0)
        self.assertEqual(self.stats().sent,4)

    def test_slow_sender_does_not_hold_mailbox(self):
        self.assertEqual(self.add(0),0)
        self.lib.vf_outbox_service_online(self.service,True)
        self.block=True
        worker=threading.Thread(target=self.step)
        worker.start()
        self.assertTrue(self.enter.wait(2))
        try:
            for n in range(1,5): self.assertEqual(self.add(n),0)
            self.assertEqual(self.add(5),-errno.EAGAIN)
            self.assertEqual(self.stats().pending_ram,4)
            self.assertTrue(worker.is_alive())
        finally:
            self.release.set(); worker.join(3)
        self.assertFalse(worker.is_alive())

    def test_storage_failure_retains_ram_and_recovers(self):
        self.path.rmdir()  # exclusive empty test directory
        self.assertEqual(self.add(0),0)
        self.assertEqual(self.step(),-errno.ENOENT)
        self.assertEqual(self.stats().pending_ram,1)
        self.assertEqual(self.lib.vf_outbox_service_destroy(self.service),-errno.EBUSY)
        self.path.mkdir()
        self.assertEqual(self.step(101),0)
        self.assertEqual(self.stats().pending_ram,0)

    def test_timeout_backoff_then_recovery(self):
        self.assertEqual(self.add(0),0)
        self.lib.vf_outbox_service_online(self.service,True)
        self.result=-errno.ETIMEDOUT
        self.assertEqual(self.step(),-errno.ETIMEDOUT)
        self.assertEqual(self.step(101),0)
        self.result=0
        self.assertEqual(self.step(102),1)
        self.assertEqual(self.step(200),0)
        self.assertEqual(len(self.calls),2)

if __name__=='__main__': unittest.main(verbosity=2)
