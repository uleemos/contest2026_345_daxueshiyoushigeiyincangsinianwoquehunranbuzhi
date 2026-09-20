#!/usr/bin/env python3
"""Host schema/error tests using actual C cloud agent, injected transport only."""
import ctypes as c
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

class CloudTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='velafit-cloud-test-')
        cls.addClassCleanup(cls.temp.cleanup)
        team = Path(__file__).resolve().parents[2]
        apps = team.parent/'apps'
        binary = Path(cls.temp.name)/'cloud.so'
        subprocess.run(['gcc', '-shared', '-fPIC', '-DOK=0',
                        '-I'+str(apps/'include'), '-I'+str(team/'app/velafit_ai/include'),
                        str(team/'app/velafit_ai/sync/velafit_cloud_agent.c'),
                        str(apps/'netutils/cjson/cJSON/cJSON.c'), '-lm', '-o', str(binary)], check=True)
        cls.lib = c.CDLL(str(binary))
        cls.callback = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_char_p, c.c_void_p, c.c_size_t)
        cls.lib.velafit_cloud_agent_submit_workout_via.argtypes = [c.c_char_p,c.c_void_p,cls.callback,c.c_void_p]

    def invoke(self, response, error=0, summary=None):
        def sender(ctx, body, out, cap):
            request = json.loads(body)
            self.assertEqual(request['model'], 'mimo-v2.5')
            self.assertEqual(request['thinking'], {'type':'disabled'})
            self.assertEqual(request['response_format'], {'type':'json_object'})
            self.assertEqual(json.loads(request['messages'][1]['content'])['session_id'], 'test')
            raw = json.dumps(response).encode()+b'\0'
            self.assertLess(len(raw), cap)
            c.memmove(out, raw, len(raw))
            return error
        out = c.create_string_buffer(2048)
        return self.lib.velafit_cloud_agent_submit_workout_via(
            (summary or '{"session_id":"test","exercise":"squat","reps":10}').encode(),
            out, self.callback(sender), None)

    @staticmethod
    def valid():
        return dict(session_id='test',score_overall=80,coach_commentary='Test summary only.',
                    next_recommended_action='Rest briefly.')

    def test_valid(self):
        self.assertEqual(self.invoke(self.valid()),0)

    def test_invalid_response(self):
        for field,value in [('score_overall',101),('score_overall',50.5),
                            ('session_id','wrong'),('coach_commentary',''),
                            ('next_recommended_action','x'*128)]:
            response=self.valid(); response[field]=value
            with self.subTest(field=field,value=value):
                self.assertLess(self.invoke(response),0)

    def test_transport_failure(self):
        self.assertEqual(self.invoke(self.valid(),error=-110),-110)

    def test_bad_input(self):
        self.assertLess(self.invoke(self.valid(),summary='{}'),0)

    def test_no_implicit_mock(self):
        out=c.create_string_buffer(2048)
        self.assertLess(self.lib.velafit_cloud_agent_submit_workout(b'{}',out),0)

if __name__=='__main__':
    unittest.main(verbosity=2)
