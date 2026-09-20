#!/usr/bin/env python3
"""Host regression for the actual NuttX FatFs open-flag conversion."""
import ctypes
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class ExclusiveFlags(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'nuttx/fs/fatfs/fatfs_vfs.c').read_text()
        function = source.split('static int fatfs_convert_oflags(int oflags)', 1)[1]
        function = function.split('\n}\n', 1)[0] + '\n}\n'
        names = ['O_RDONLY', 'O_WRONLY', 'O_CREAT', 'O_EXCL', 'O_TRUNC', 'O_APPEND',
                 'FA_READ', 'FA_WRITE', 'FA_CREATE_NEW', 'FA_CREATE_ALWAYS',
                 'FA_OPEN_ALWAYS', 'FA_OPEN_APPEND']
        headers = (ROOT / 'nuttx/include/fcntl.h').read_text()
        headers += (ROOT / 'nuttx/fs/fatfs/fatfs/source/ff.h').read_text()
        defines = []
        for name in names:
            match = re.search(r'^#define\s+' + name + r'\s+([^\r\n]+)', headers, re.M)
            if not match:
                raise RuntimeError('Missing actual header definition: ' + name)
            defines.append('#define ' + name + ' ' + match.group(1))
        cls.tmp = tempfile.TemporaryDirectory(prefix='velafit-fatfs-flags-')
        cls.addClassCleanup(cls.tmp.cleanup)
        path = Path(cls.tmp.name)
        code = '\n'.join(defines) + '\nint convert(int oflags)' + function
        code += '\nint constant(int i) { switch(i) {\n'
        for i, name in enumerate(names):
            code += f'case {i}: return {name};\n'
        code += '} return -1; }\n'
        (path / 'flags.c').write_text(code)
        subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Werror',
                        str(path / 'flags.c'), '-o', str(path / 'flags.so')], check=True)
        cls.lib = ctypes.CDLL(str(path / 'flags.so'))
        cls.flags = {name: cls.lib.constant(i) for i, name in enumerate(names)}

    def test_exclusive_overrides_create_append_truncate(self):
        f = self.flags
        for access in [f['O_RDONLY'], f['O_WRONLY'], f['O_RDONLY'] | f['O_WRONLY']]:
            for extra in [0, f['O_APPEND'], f['O_TRUNC'], f['O_APPEND'] | f['O_TRUNC']]:
                with self.subTest(access=access, extra=extra):
                    actual = self.lib.convert(access | f['O_CREAT'] | f['O_EXCL'] | extra)
                    self.assertEqual(actual, access | f['FA_CREATE_NEW'])

    def test_nonexclusive_modes_unchanged(self):
        f = self.flags
        for mode, expected in [('O_CREAT', 'FA_OPEN_ALWAYS'),
                               ('O_TRUNC', 'FA_CREATE_ALWAYS'),
                               ('O_APPEND', 'FA_OPEN_APPEND')]:
            self.assertEqual(self.lib.convert(f['O_WRONLY'] | f[mode]),
                             f['FA_WRITE'] | f[expected])
        self.assertEqual(self.lib.convert(f['O_RDONLY']), f['FA_READ'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
