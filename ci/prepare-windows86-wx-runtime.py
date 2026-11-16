"""Complete the legacy Win32 SDK with its pinned release GUI DLLs for CTest."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
NAME = 'wxMSW-3.2.1_vc14x_ReleaseDLL.7z'
SHA256 = '9b0d445ce56c1625b884314ec5cf043196c294c42624163a3e7f836fa4696bc8'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    library = Path(sys.argv[1]).resolve()
    sdk = ROOT / 'cache/wxWidgets-3.2.1'
    if library != (sdk / 'lib/vc14x_dll').resolve() or not (sdk / 'include/wx/version.h').is_file():
        raise RuntimeError('Select the existing pinned wxWidgets 3.2.1 Win32 SDK')
    archive = ROOT / 'cache' / NAME
    if not archive.is_file():
        temporary = archive.with_suffix('.part')
        with urllib.request.urlopen('https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.1/' + NAME,
                                    timeout=90) as response, temporary.open('wb') as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        if digest(temporary) != SHA256:
            temporary.unlink()
            raise RuntimeError('Win32 wxWidgets release runtime checksum mismatch')
        temporary.replace(archive)
    if digest(archive) != SHA256:
        raise RuntimeError('Win32 wxWidgets release runtime checksum mismatch')
    subprocess.run(['7z', 'x', '-y', '-o' + str(sdk), str(archive)], check=True)
    required = ['wxbase32u_vc14x.dll', 'wxbase32u_net_vc14x.dll', 'wxbase32u_xml_vc14x.dll',
                'wxmsw32u_core_vc14x.dll', 'wxmsw32u_aui_vc14x.dll',
                'wxmsw32u_html_vc14x.dll', 'wxmsw32u_adv_vc14x.dll', 'wxmsw32u_gl_vc14x.dll']
    images = {}
    for name in required:
        data = (library / name).read_bytes()
        if data[:2] != b'MZ':
            raise RuntimeError('Invalid Win32 wxWidgets runtime: ' + name)
        offset = struct.unpack_from('<I', data, 0x3c)[0]
        if data[offset:offset + 4] != b'PE\0\0' or struct.unpack_from('<H', data, offset + 4)[0] != 0x14c:
            raise RuntimeError('Wrong architecture in the Win32 wxWidgets runtime: ' + name)
        images[name] = hashlib.sha256(data).hexdigest()
    receipt = dict(version='3.2.1', architecture='x86', archive=NAME, archive_sha256=SHA256,
                   gui_dlls=images, purpose='CI test SDK only; not bundled into the plugin')
    (ROOT / 'cache/wx321-runtime.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Pinned release Win32 wxWidgets test runtime verified', flush=True)


if __name__ == '__main__':
    main()
