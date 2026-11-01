"""Validate the built Windows version resource without executing the binary."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys


def verify(binary, expected_version):
    if sys.platform != 'win32':
        raise RuntimeError('Windows version APIs are required')
    binary = Path(binary).resolve(strict=True)
    api = ctypes.WinDLL(str(Path(os.environ['SystemRoot']) / 'System32/version.dll'), use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
    api.GetFileVersionInfoW.restype = wintypes.BOOL
    api.VerQueryValueW.argtypes = [wintypes.LPCVOID, wintypes.LPCWSTR,
                                 ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    api.VerQueryValueW.restype = wintypes.BOOL
    unused = wintypes.DWORD()
    size = api.GetFileVersionInfoSizeW(str(binary), ctypes.byref(unused))
    if not size:
        raise ctypes.WinError(ctypes.get_last_error())
    data = ctypes.create_string_buffer(size)
    if not api.GetFileVersionInfoW(str(binary), 0, size, data):
        raise ctypes.WinError(ctypes.get_last_error())

    def query(key):
        pointer, length = ctypes.c_void_p(), wintypes.UINT()
        if not api.VerQueryValueW(data, key, ctypes.byref(pointer), ctypes.byref(length)):
            raise RuntimeError(f'Missing version resource: {key}')
        return pointer, length.value

    pointer, length = query('\\')
    if length < 52:
        raise RuntimeError('Incomplete fixed version information')
    fixed = struct.unpack('<13I', ctypes.string_at(pointer, 52))
    expected = tuple(map(int, expected_version.split('.')))
    if len(expected) != 4 or any(not 0 <= part <= 65535 for part in expected):
        raise ValueError('Expected version must have four unsigned 16-bit components')
    if fixed[0] != 0xFEEF04BD:
        raise RuntimeError('Invalid fixed version signature')
    for offset in (2, 4):
        actual = (fixed[offset] >> 16, fixed[offset] & 65535,
                  fixed[offset + 1] >> 16, fixed[offset + 1] & 65535)
        if actual != expected:
            raise RuntimeError(f'Fixed version {actual} differs from {expected}')
    pointer, length = query('\\VarFileInfo\\Translation')
    if not length or length % 4:
        raise RuntimeError('Invalid translation array')
    translations = struct.iter_unpack('<HH', ctypes.string_at(pointer, length))
    required = {'CompanyName': 'IxView', 'ProductName': 'uxzView',
                'FileDescription': 'uxzView Image Viewer', 'InternalName': 'uxzView',
                'OriginalFilename': 'uxzView.exe', 'LegalCopyright': 'GPL-3.0-or-later',
                'FileVersion': expected_version, 'ProductVersion': expected_version}
    tables = {}
    for language, codepage in translations:
        table = f'{language:04x}{codepage:04x}'
        fields = {}
        for key, value in required.items():
            pointer, length = query(f'\\StringFileInfo\\{table}\\{key}')
            actual = ctypes.wstring_at(pointer, length).rstrip('\0')
            if actual != value:
                raise RuntimeError(f'{table}/{key}: expected {value!r}, got {actual!r}')
            fields[key] = actual
        tables[table] = fields
    with binary.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'file': binary.name, 'sha256': digest, 'version': expected_version, 'tables': tables}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--version', help='Expected four-part Windows version; defaults to CMake project version + .0')
    args = parser.parse_args()
    expected = args.version
    if expected is None:
        source = (Path(__file__).resolve().parents[1] / 'CMakeLists.txt').read_text(encoding='utf-8-sig')
        match = re.search(r'project\(IxView VERSION (\d+\.\d+\.\d+)', source)
        if not match:
            raise RuntimeError('Cannot find the CMake project version; pass --version explicitly')
        expected = match[1] + '.0'
    print(json.dumps(verify(args.binary, expected), indent=2))
