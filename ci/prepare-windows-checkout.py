#!/usr/bin/env python3
"""Materialize frozen inputs from the index in a fresh Windows CI checkout."""
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    manifest = root / 'testdata/engine-freeze-2.2.sha256'
    paths = []
    for entry in manifest.read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'[0-9a-f]{64}  (.+)', entry)
        if match:
            paths.append(match.group(1))
    if not paths:
        raise RuntimeError('Frozen source manifest is empty')

    # Git can keep files from the default branch when it switches branches,
    # even if the target branch disables CRLF conversion for those paths.
    # Check out the index to an empty directory so Git cannot reuse its
    # cached stat information for a file already in the working tree.
    with tempfile.TemporaryDirectory(dir=root) as directory:
        staged = Path(directory)
        path_list = b'\0'.join(os.fsencode(path) for path in paths) + b'\0'
        subprocess.run(['git', 'checkout-index', '--stdin', '-z',
                        f'--prefix={staged.name}/'],
                       cwd=root, input=path_list, check=True)
        for path in paths:
            expected = (staged / path).read_bytes()
            current = root / path
            if current.read_bytes() != expected:
                current.write_bytes(expected)


if __name__ == '__main__':
    main()
