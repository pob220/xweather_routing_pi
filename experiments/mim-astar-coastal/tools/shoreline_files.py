#!/usr/bin/env python3
"""Find existing OpenCPN-format GSHHG files; never download or change them."""
import argparse
import gzip
import struct
import warnings
import json
import os
from pathlib import Path


def candidates(roots=None):
    if roots is None:
        data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
        repository = Path(__file__).resolve().parents[3]
        roots = [data / 'opencpn', Path.home() / '.opencpn',
                 Path('/usr/share/opencpn'), Path('/usr/local/share/opencpn'),
                 repository / 'data/shoreline']
    found = {}
    # Limit traversal to known OpenCPN layouts; do not crawl the home directory.
    filenames = ['poly-*-2.3.7.dat*', 'poly-*-1.dat', 'poly-*-1.dat.gz']
    folders = ['', 'shoreline/', 'gshhs/', 'data/', 'data/shoreline/',
               'plugins/*/data/', 'plugins/*/data/shoreline/', 'plugins/*/data/gshhs/']
    patterns = [folder + name for folder in folders for name in filenames]
    for root in roots:
        for pattern in patterns:
            for file in Path(root).glob(pattern):
                if file.is_file() and file.stat().st_size:
                    try:
                        opener = gzip.open if file.suffix == '.gz' else open
                        with opener(file, 'rb') as source:
                            header = struct.unpack('<7i', source.read(28))
                        if 210 <= header[0] <= 240 and header[1:] == (1, 1, 0, -90, 360, 90):
                            found[str(file.resolve())] = file.resolve()
                        else:
                            warnings.warn(f'Skipping unsupported shoreline header: {file}')
                    except (OSError, EOFError, struct.error) as error:
                        warnings.warn(f'Skipping unreadable shoreline file {file}: {error}')
    return sorted(found.values(), key=str)


def select(files, coarse=False):
    order = 'clihf' if coarse else 'fhilc'
    for resolution in order:
        selected = [p for p in files if p.name.startswith('poly-' + resolution + '-')]
        if selected:
            return selected[0]
    raise FileNotFoundError('No usable OpenCPN poly-[fhilc]-1.dat or versioned .dat[.gz] file found; supply an explicit path')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', action='append', type=Path)
    args = parser.parse_args()
    files = candidates(args.root)
    print(json.dumps({'files': [str(p) for p in files],
                      'finest': str(select(files)) if files else None}, indent=2))
