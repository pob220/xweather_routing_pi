"""The frozen-input guard must see repository bytes on Windows checkouts."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('git'), 'requires Git attribute resolution')
class FrozenCheckoutAttributes(unittest.TestCase):
    def test_every_frozen_input_preserves_its_repository_bytes(self):
        manifest = ROOT / 'testdata/engine-freeze-2.2.sha256'
        paths = [match.group(1) for line in manifest.read_text().splitlines()
                 if (match := re.fullmatch(r'[0-9a-f]{64}  (.+)', line))]
        self.assertTrue(paths)
        with tempfile.TemporaryDirectory() as temporary:
            attributes = Path(temporary) / 'empty-attributes'
            attributes.write_text('')
            result = subprocess.run(
                ['git', '-c', f'core.attributesFile={attributes}',
                 'check-attr', '-z', '--stdin', 'text'],
                cwd=ROOT, env=dict(os.environ, GIT_ATTR_NOSYSTEM='1'),
                input=b'\0'.join(path.encode() for path in paths) + b'\0',
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        fields = result.stdout.decode().rstrip('\0').split('\0')
        self.assertEqual(len(fields), len(paths) * 3)
        unsafe = [f'{fields[i]}: text={fields[i + 2]}'
                  for i in range(0, len(fields), 3) if fields[i + 2] != 'unset']
        self.assertFalse(unsafe, 'Frozen files permit checkout conversion:\n' + '\n'.join(unsafe))


if __name__ == '__main__':
    unittest.main()
