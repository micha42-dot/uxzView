"""Build the pinned metadata library for local files, without HTTP/Curl readers.

Image formats, XMP, PNG, BMFF, Brotli, video metadata and localization remain
enabled. App/extension downloads use Qt Network, independently of Exiv2.
"""
from build_heif import build_codec

VERSION = '0.28.9'
SOURCE_URL = 'https://repo.msys2.org/mingw/sources/mingw-w64-exiv2-0.28.9-2.src.tar.zst'
SOURCE_SHA256 = 'bae8aa45caab4bf61ecd9d2265089f3f412c4885625b37d03966d7bbdb560a26'
UPSTREAM_SHA256 = '700b76b97695b2fab4ef8c79619c68ae57d09e0c130724791cafbd39e0eb4aef'
OPTIONS = {
    'BUILD_SHARED_LIBS': 'ON', 'CMAKE_DLL_NAME_WITH_SOVERSION': 'ON',
    'EXIV2_ENABLE_WEBREADY': 'OFF', 'EXIV2_ENABLE_CURL': 'OFF',
    'EXIV2_ENABLE_XMP': 'ON', 'EXIV2_ENABLE_PNG': 'ON', 'EXIV2_ENABLE_BMFF': 'ON',
    'EXIV2_ENABLE_BROTLI': 'ON', 'EXIV2_ENABLE_VIDEO': 'ON', 'EXIV2_ENABLE_NLS': 'ON',
    'EXIV2_BUILD_EXIV2_COMMAND': 'OFF', 'EXIV2_BUILD_SAMPLES': 'OFF',
    'EXIV2_BUILD_UNIT_TESTS': 'OFF', 'EXIV2_BUILD_FUZZ_TESTS': 'OFF', 'EXIV2_BUILD_DOC': 'OFF',
}


def build():
    return build_codec(name='exiv2', version=VERSION, source_url=SOURCE_URL,
                       source_sha256=SOURCE_SHA256, upstream_sha256=UPSTREAM_SHA256,
                       options=OPTIONS, library_path='bin/libexiv2-28.dll',
                       target='exiv2lib', recipe_path=__file__)


if __name__ == '__main__':
    print(build())
