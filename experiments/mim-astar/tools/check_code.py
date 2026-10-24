#!/usr/bin/env python3
"""Check formatting, selected guideline rules and standalone header inclusion.

Requires a configured CMake build, clang-format and clang-tidy. Dependencies are
not rewritten or presented as conforming merely because our header filter omits
their diagnostics. Compiler commands are executed as argument lists, not shells.
"""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

EXPERIMENT = Path(__file__).resolve().parents[1]


def syntax_command(entry, source):
    arguments = entry.get('arguments') or shlex.split(entry['command'])
    command = []
    skip_next = False
    for argument in arguments:
        if skip_next:
            skip_next = False
        elif argument in ('-o', '-MF', '-MT', '-MQ'):
            skip_next = True
        elif argument not in ('-c', '-MD', '-MMD', entry['file']):
            command.append(argument)
    return [*command, '-fsyntax-only', str(source)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    args.build = args.build.resolve()
    for name in ('clang-format', 'clang-tidy'):
        if not shutil.which(name):
            parser.error(f'Install {name} to run this review')
    files = sorted(path for folder in ('src', 'include', 'tests')
                   for path in (EXPERIMENT / folder).rglob('*')
                   if path.suffix in ('.h', '.cpp'))
    subprocess.run(['clang-format', '--dry-run', '--Werror', *map(str, files)], check=True)
    subprocess.run(['clang-tidy', '-p', str(args.build),
                    str(EXPERIMENT / 'src/Router.cpp')], check=True)
    entries = json.loads((args.build / 'compile_commands.json').read_text())
    for source, headers in (
            ('src/Router.cpp', ('include/mim_astar/Router.h', 'src/SearchStorage.h')),
            ('tests/HeadlessContracts.cpp', ('tests/BenchmarkBounds.h', 'tests/HeadlessFixtures.h'))):
        entry = next(item for item in entries
                     if Path(item['file']).resolve() == EXPERIMENT / source)
        with tempfile.TemporaryDirectory(prefix='mim-header-check-', dir=args.build) as folder:
            probe = Path(folder) / 'header.cpp'
            for header in headers:
                probe.write_text(f'#include "{EXPERIMENT / header}"\n')
                subprocess.run(syntax_command(entry, probe), cwd=entry['directory'], check=True)
    print('Formatting, configured clang-tidy checks and four standalone headers passed.')


if __name__ == '__main__':
    main()
