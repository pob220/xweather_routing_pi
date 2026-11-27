#!/usr/bin/env python3
"""Reuse the unchanged, natively qualified AMD64 helper."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_SHA256 = 'b6cd3eeee6b3856d884241ccabcf709d46d96082ecb6c470124733ff64c3f8ed'
URL = ('https://github.com/pob220/xweather_routing_pi/releases/download/'
       'developer-test-2.13.2/'
       'xweather_routing_pi-2.13.2-msvc-64-x86_64-10.0-MSVC-x64-wx334.tar.gz')
BASELINE = 'd6324351ea0aad81f80fab69a842fe7d1a7a1188'
INPUTS = ['third_party/environmental-grib-generator', 'ci/windows-generator',
          'ci/vcpkg-overlay-ports', 'ci/vcpkg-triplets',
          'ci/build-windows-generator.py', 'ci/windows_build_tools.py']


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def unchanged_inputs():
    exists = subprocess.run(['git', '-C', str(ROOT), 'cat-file', '-e', BASELINE + '^{commit}'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if exists.returncode:
        subprocess.run(['git', '-C', str(ROOT), 'fetch', '--no-tags', 'origin', BASELINE], check=True)
    result = subprocess.run(['git', '-C', str(ROOT), 'diff', '--quiet', BASELINE,
                             '--', *INPUTS])
    if result.returncode not in (0, 1):
        result.check_returncode()
    return result.returncode == 0


def stage(archive, destination):
    if digest(archive) != ARCHIVE_SHA256:
        raise RuntimeError('Qualified helper archive checksum mismatch')
    selected = []
    with tarfile.open(archive, 'r:gz') as package:
        for member in package:
            path = PurePosixPath(member.name)
            if path.is_absolute() or '..' in path.parts or member.issym() or member.islnk():
                raise RuntimeError('Unsafe helper archive path')
            if not member.isfile():
                continue
            parts = path.parts
            for n in range(len(parts)-2):
                if parts[n:n+2] == ('plugins', 'xweather_routing_pi'):
                    relative = PurePosixPath(*parts[n+2:])
                    if relative.parts[0] in ('bin', 'runtime'):
                        selected.append((member, relative))
                    break
        receipts = [member for member, relative in selected
                    if relative.as_posix() == 'runtime/stage.json']
        if len(receipts) != 1:
            raise RuntimeError('Missing helper provenance')
        receipt = json.load(package.extractfile(receipts[0]))
        source = ROOT / 'third_party/environmental-grib-generator'
        actual = {p.relative_to(source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in source.rglob('*') if p.is_file()}
        crlf = {p.relative_to(source).as_posix(): hashlib.sha256(
                    p.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')).hexdigest()
                for p in source.rglob('*') if p.is_file()}
        if receipt['architecture'] != 'x86_64' or receipt['source_sha256'] not in (actual, crlf):
            raise RuntimeError('Scientific helper source/architecture changed; rebuild and requalify it')
        for member, relative in selected:
            output = destination.joinpath(*relative.parts)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(package.extractfile(member).read())
    binaries = list((destination/'bin').glob('*.dll')) + list((destination/'bin').glob('*.exe'))
    if not (destination/'bin/environmental-grib.exe').is_file():
        raise RuntimeError('Missing independent helper executable')
    for binary in binaries:
        data = binary.read_bytes()
        if data[:2] != b'MZ' or len(data) < 64:
            raise RuntimeError('Invalid helper image: ' + binary.name)
        offset = struct.unpack_from('<I', data, 0x3c)[0]
        if data[offset:offset+4] != b'PE\0\0' or struct.unpack_from('<H', data, offset+4)[0] != 0x8664:
            raise RuntimeError('Helper image is not AMD64: ' + binary.name)
    (destination/'qualified-reuse.json').write_text(json.dumps(dict(
        archive=URL, archive_sha256=ARCHIVE_SHA256, unchanged_helper_inputs=True,
        source_files=len(actual), amd64_images=len(binaries)), indent=2)+'\n')
    print(f'Qualified unchanged AMD64 helper staged: {len(actual)} sources, {len(binaries)} native images')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--destination', type=Path, default=ROOT/'.windows-deps/stage')
    parser.add_argument('--build-if-changed', action='store_true',
                        help='Build the native helper if its qualified inputs have changed')
    args = parser.parse_args()
    if not unchanged_inputs():
        if not args.build_if_changed:
            raise RuntimeError('Scientific helper inputs changed; rebuild and requalify it')
        print('Scientific helper inputs changed; building the native helper', flush=True)
        subprocess.run([sys.executable, str(ROOT / 'ci/build-windows-generator.py')],
                       check=True)
        return
    archive = args.archive or ROOT/'.windows-deps/qualified-helper.tar.gz'
    if not archive.exists():
        archive.parent.mkdir(parents=True, exist_ok=True)
        temporary = archive.with_suffix('.part')
        urllib.request.urlretrieve(URL, temporary)
        if digest(temporary) != ARCHIVE_SHA256:
            raise RuntimeError('Downloaded helper archive checksum mismatch')
        temporary.replace(archive)
    stage(archive, args.destination)


if __name__ == '__main__':
    main()
