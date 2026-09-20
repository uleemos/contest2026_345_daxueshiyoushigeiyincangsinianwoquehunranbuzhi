#!/usr/bin/env python3
import ctypes as c
import errno
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

class OutboxTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='velafit-outbox-build-')
        cls.addClassCleanup(cls.temp.cleanup)
        team=Path(__file__).resolve().parents[2]; apps=team.parent/'apps'
        binary=Path(cls.temp.name)/'outbox.so'
        subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-shared','-fPIC',
                        '-I'+str(apps/'include'),str(team/'app/velafit_ai/sync/velafit_outbox.c'),
                        str(apps/'netutils/cjson/cJSON/cJSON.c'),'-lm','-o',str(binary)],check=True)
        cls.lib=c.CDLL(str(binary))
        cls.callback=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_char_p)
        cls.lib.vf_outbox_enqueue.argtypes=[c.c_char_p]*3
        cls.lib.vf_outbox_is_acked.argtypes=[c.c_char_p]*2
        cls.lib.vf_outbox_tick.argtypes=[c.c_char_p,c.c_int64,cls.callback,c.c_void_p]

    def setUp(self):
        self.tempdir=tempfile.TemporaryDirectory(prefix='velafit-outbox-data-')
        self.addCleanup(self.tempdir.cleanup)
        self.directory=self.tempdir.name.encode(); self.sent=[]

    def enqueue(self,id='test',reps=10):
        return self.lib.vf_outbox_enqueue(self.directory,id.encode(),
                   json.dumps(dict(session_id=id,exercise='squat',reps=reps)).encode())

    def record_path(self):
        return next(Path(self.tempdir.name).glob('*.json'))

    def tick(self,now,result=0):
        def sender(ctx,body):
            self.sent.append(json.loads(body)); return result
        return self.lib.vf_outbox_tick(self.directory,now,self.callback(sender),None)

    def test_fail_recover_dedup(self):
        self.assertEqual(self.enqueue(),0)
        self.assertEqual(self.tick(100,-errno.ETIMEDOUT),-errno.ETIMEDOUT)
        self.assertEqual(self.tick(101),0)
        # Each tick rereads persisted state; no in-memory queue survives calls.
        self.assertEqual(self.tick(102),1)
        self.assertEqual(self.enqueue(),0)
        self.assertEqual(self.tick(200),0)
        self.assertEqual(len(self.sent),2)
        record=json.loads(Path(str(self.record_path())+'.state02').read_text())
        self.assertTrue(record['done']); self.assertEqual(record['attempts'],2)

    def test_id_conflict_and_path(self):
        self.assertEqual(self.enqueue(),0)
        self.assertEqual(self.enqueue(reps=20),-errno.EEXIST)
        self.assertEqual(self.enqueue('../escape'),-errno.EINVAL)

    def test_ack_requires_explicit_persisted_success(self):
        ack=lambda: self.lib.vf_outbox_is_acked(self.directory,b'test')
        self.assertEqual(ack(),-errno.ENOENT)
        self.assertEqual(self.enqueue(),0)
        self.assertEqual(ack(),0)
        self.assertEqual(self.tick(1800000000,-errno.ENETDOWN),-errno.ENETDOWN)
        self.assertEqual(self.tick(0),0)  # unset RTC does not prove ACK
        self.assertEqual(ack(),0)
        self.assertEqual(self.tick(1800000002),1)
        self.assertEqual(ack(),1)

    def test_permanent_failure_is_not_ack(self):
        self.assertEqual(self.enqueue(),0)
        self.assertEqual(self.tick(100,-errno.EACCES),-errno.EACCES)
        self.assertEqual(self.tick(200),0)
        self.assertEqual(self.lib.vf_outbox_is_acked(self.directory,b'test'),0)

    def test_permanent_failure_stops(self):
        self.assertEqual(self.enqueue(),0)
        self.assertEqual(self.tick(100,-errno.EACCES),-errno.EACCES)
        self.assertEqual(self.tick(10000),0)
        self.assertEqual(len(self.sent),1)

    def test_capacity(self):
        for i in range(32): self.assertEqual(self.enqueue(f's{i}'),0)
        self.assertEqual(self.enqueue('extra'),-errno.ENOSPC)

    def test_corrupt_record_preserved(self):
        self.assertEqual(self.enqueue(),0)
        # Corrupt via standard C-library truncate; no repair/delete on read failure.
        file=self.record_path()
        with file.open('r+b') as f: f.truncate(4)
        self.assertEqual(self.tick(100),-errno.EBADMSG)
        self.assertTrue(file.exists()); self.assertEqual(self.sent,[])

    def test_original_immutable(self):
        self.assertEqual(self.enqueue(),0)
        file=self.record_path()
        original=file.read_bytes()
        self.assertEqual(self.tick(100,-errno.ETIMEDOUT),-errno.ETIMEDOUT)
        self.assertEqual(self.tick(102),1)
        self.assertEqual(file.read_bytes(),original)
        self.assertEqual(len(list(Path(self.tempdir.name).iterdir())),3)

    def test_torn_generation_blocks_without_resend(self):
        self.assertEqual(self.enqueue(),0)
        self.assertEqual(self.tick(100,-errno.ETIMEDOUT),-errno.ETIMEDOUT)
        file=Path(str(self.record_path())+'.state01')
        with file.open('r+b') as f: f.truncate(8)
        self.sent.clear()
        self.assertEqual(self.tick(102),-errno.EBADMSG)
        self.assertEqual(self.sent,[])

    def test_attempts_bounded(self):
        self.assertEqual(self.enqueue(),0)
        for i in range(10):
            self.assertEqual(self.tick(i*300,-errno.ETIMEDOUT),-errno.ETIMEDOUT)
        self.assertEqual(self.tick(9999),0)
        self.assertEqual(len(self.sent),10)
        self.assertEqual(len(list(Path(self.tempdir.name).iterdir())),11)

    def test_target_name_max(self):
        self.assertEqual(self.enqueue('x'*31),0)
        self.assertEqual(self.tick(100),1)
        self.assertTrue(all(len(p.name)<=32 for p in Path(self.tempdir.name).iterdir()))

    def test_corruption_does_not_starve_other_sessions(self):
        self.assertEqual(self.enqueue('broken'),0)
        broken=self.record_path()
        with broken.open('r+b') as f: f.truncate(4)
        preserved=broken.read_bytes()
        for i in range(3): self.assertEqual(self.enqueue(f'valid{i}'),0)
        for i in range(3): self.assertEqual(self.tick(100),1)
        self.assertEqual(self.tick(100),-errno.EBADMSG)
        self.assertEqual(len(self.sent),3)
        self.assertEqual(broken.read_bytes(),preserved)

    def test_scan_reports_preserved_corruption(self):
        class Scan(c.Structure):
            _fields_=[('corrupt_records',c.c_uint),('visited_records',c.c_uint),('io_errors',c.c_uint)]
        self.lib.vf_outbox_tick_report.argtypes=[c.c_char_p,c.c_int64,self.callback,
                                               c.c_void_p,c.POINTER(Scan)]
        self.assertEqual(self.enqueue(),0)
        with self.record_path().open('r+b') as f: f.truncate(4)
        scan=Scan()
        result=self.lib.vf_outbox_tick_report(self.directory,100,
                                            self.callback(lambda ctx,body:0),None,c.byref(scan))
        self.assertEqual(result,-errno.EBADMSG)
        self.assertEqual((scan.corrupt_records,scan.visited_records),(1,1))

    def test_scan_io_failure_is_not_json_corruption(self):
        class Scan(c.Structure):
            _fields_=[('corrupt_records',c.c_uint),('visited_records',c.c_uint),('io_errors',c.c_uint)]
        self.lib.vf_outbox_tick_report.argtypes=[c.c_char_p,c.c_int64,self.callback,
                                               c.c_void_p,c.POINTER(Scan)]
        (Path(self.tempdir.name)/'read-error.json').mkdir()
        scan=Scan()
        result=self.lib.vf_outbox_tick_report(self.directory,100,
                  self.callback(lambda ctx,body:0),None,c.byref(scan))
        self.assertEqual(result,-errno.EIO)
        self.assertEqual((scan.corrupt_records,scan.visited_records,scan.io_errors),(0,1,1))

if __name__=='__main__': unittest.main(verbosity=2)
