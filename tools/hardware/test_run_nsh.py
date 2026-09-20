#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch
import run_nsh


class RunnerTimeout(unittest.TestCase):
    def test_partial_output_stops_later_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'serial.log'
            port = MagicMock()
            port.read.return_value = b''
            with patch('sys.argv', ['run_nsh', '--log', str(log), 'probe', 'free']), \
                 patch.object(run_nsh, 'open_port', return_value=port), \
                 patch.object(run_nsh, 'write_command') as write, \
                 patch.object(run_nsh.time, 'sleep'), \
                 patch.object(run_nsh, 'read_until_prompt', side_effect=[b'nsh>', b'probe\r\npartial']):
                self.assertEqual(run_nsh.main(), 2)
                self.assertEqual(write.call_count, 2)  # initial newline + probe
            port.close.assert_called_once()
            self.assertIn(b'FAIL: command', log.read_bytes())


if __name__ == '__main__':
    unittest.main()
