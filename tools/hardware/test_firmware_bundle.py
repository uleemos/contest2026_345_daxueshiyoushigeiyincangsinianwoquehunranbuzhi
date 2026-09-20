#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host fake-artifact validation only; never opens serial ports."""
import json
from pathlib import Path
import tempfile
import unittest
import firmware_bundle as f

class BundleTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='vf-bundle-test-')
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        for name,data in {'nuttx.bin':b'\xe9fake-image','nuttx.elf':b'\x7fELFfake',
                          'config':b'CONFIG_TEST=y\n'}.items():
            (self.root/name).write_bytes(data)
        self.manifest=dict(chip='esp32p4',offset='0x2000',
                           files={name:f.digest(self.root/name) for name in f.FILES})
        (self.root/'manifest.json').write_text(json.dumps(self.manifest))

    def test_verify(self):
        self.assertEqual(f.verify(self.root),self.manifest)

    def test_tamper_rejected(self):
        (self.root/'nuttx.bin').write_bytes(b'\xe9different')
        with self.assertRaises(ValueError): f.verify(self.root)

    def test_bad_platform_rejected(self):
        self.manifest['chip']='esp32c6'
        (self.root/'manifest.json').write_text(json.dumps(self.manifest))
        with self.assertRaises(ValueError): f.verify(self.root)

    def test_missing_rejected(self):
        # Temporary test artifact only; no user project file is removed.
        (self.root/'config').unlink()
        with self.assertRaises(ValueError): f.verify(self.root)

    def test_port_and_offset(self):
        cmd=f.flash_command(self.root,'python3','COM3')
        self.assertIn('0x2000',cmd)
        self.assertNotIn('erase-flash',cmd)
        with self.assertRaises(ValueError): f.flash_command(self.root,'python3','COM3;bad')

    def test_snapshot_outside_private_rejected(self):
        with self.assertRaises(ValueError): f.snapshot(self.root/'new',[])

if __name__=='__main__': unittest.main(verbosity=2)
