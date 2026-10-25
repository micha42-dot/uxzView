"""Build the pinned libavif using AOM/dav1d without redundant AV1 encoders."""
from build_heif import build_codec
VERSION = '1.4.2'
SOURCE_URL = 'https://repo.msys2.org/mingw/sources/mingw-w64-libavif-1.4.2-1.src.tar.zst'
SOURCE_SHA256 = '54c85b5a51e136c181d9a084b60b881b373b1f7574b0fd0d560ed444117619b5'
UPSTREAM_SHA256 = '2b645287340ba5a631d268b551dc2d72bd73ac33335962dd36dcdb6d8366921d'
OPTIONS = {'BUILD_SHARED_LIBS':'ON','CMAKE_DLL_NAME_WITH_SOVERSION':'ON',
           'AVIF_BUILD_APPS':'OFF','AVIF_BUILD_TESTS':'OFF','AVIF_BUILD_GDK_PIXBUF':'OFF',
           'AVIF_CODEC_AOM':'SYSTEM','AVIF_CODEC_AOM_DECODE':'ON','AVIF_CODEC_AOM_ENCODE':'ON',
           'AVIF_CODEC_DAV1D':'SYSTEM','AVIF_CODEC_RAV1E':'OFF','AVIF_CODEC_SVT':'OFF',
           'AVIF_CODEC_LIBGAV1':'OFF','AVIF_CODEC_AVM':'OFF',
           'AVIF_LIBYUV':'SYSTEM','AVIF_LIBSHARPYUV':'SYSTEM','AVIF_LIBXML2':'SYSTEM'}

def build():
    return build_codec(name='libavif',version=VERSION,source_url=SOURCE_URL,
                       source_sha256=SOURCE_SHA256,upstream_sha256=UPSTREAM_SHA256,options=OPTIONS,
                       library_path='libavif-16.dll',target='avif',recipe_path=__file__)

if __name__=='__main__':
    print(build())
