# SPDX-License-Identifier: GPL-3.0-or-later
# Guards for Apple toolchain combinations we cannot repair from our sources.

# Qt < 6.9 links -framework AGL from its own FindWrapOpenGL.cmake. Apple
# removed that framework from recent SDKs, so the mismatch surfaces as a link
# error inside Qt instead of as a statement about the toolchain. Detect it at
# configure time and name the way out. No source change in uxzView could fix
# this: the reference is not in this repository (AGL appears 0 times here).
function(ix_check_apple_opengl_framework)
    if(NOT APPLE OR NOT Qt6_VERSION)
        return()
    endif()
    if(NOT Qt6_VERSION VERSION_LESS 6.9)
        return()
    endif()
    option(IX_ALLOW_MISSING_AGL
        "Configure even though Qt < 6.9 needs the AGL framework, which this SDK does not provide" OFF)
    if(IX_ALLOW_MISSING_AGL)
        return()
    endif()
    find_library(IX_AGL_FRAMEWORK AGL)
    if(IX_AGL_FRAMEWORK)
        return()
    endif()
    message(FATAL_ERROR
        "Qt ${Qt6_VERSION} links against the AGL framework, which the installed macOS SDK no longer provides. "
        "That reference lives in Qt's own FindWrapOpenGL.cmake, not in uxzView, so nothing here can work around it. "
        "Build with Qt 6.9 or newer (6.11.3 has been verified on recent SDKs), select an SDK that still ships AGL, "
        "or pass -DIX_ALLOW_MISSING_AGL=ON if this Qt was built without AGL support.")
endfunction()
