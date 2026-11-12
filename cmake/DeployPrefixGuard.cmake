# SPDX-License-Identifier: GPL-3.0-or-later
# Included from the generated install script (IX_DEPLOY_QT) BEFORE Qt deploys
# the bundle, so the effective --prefix is known here.
#
# macdeployqt rewrites install names to "@loader_path/../../<absolute path>"
# when the destination path contains non-ASCII characters. Such a bundle only
# starts at its original location and fails as soon as it is moved, which is
# exactly what a deployment artifact must survive. Fail loudly instead of
# shipping it.
#
# A correctly deployed bundle is relocatable because its install names are
# relative, so installing into an ASCII path and moving the finished
# uxzView.app afterwards produces the same, working result.
if(NOT APPLE)
    return()
endif()
string(REGEX MATCH "[^ -~]" ix_deploy_non_ascii "${CMAKE_INSTALL_PREFIX}")
if(NOT ix_deploy_non_ascii)
    return()
endif()
message(FATAL_ERROR
    "The install prefix contains non-ASCII characters (\"${CMAKE_INSTALL_PREFIX}\"). "
    "macdeployqt rewrites Qt's install names to broken absolute @loader_path entries in that case, "
    "and the deployed application will not start once it is moved. "
    "Install into an ASCII path (e.g. build/deploy) and move the finished uxzView.app afterwards.")
