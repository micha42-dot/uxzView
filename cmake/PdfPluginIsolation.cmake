# SPDX-License-Identifier: GPL-3.0-or-later
# Qt's generic qpdf handler eagerly maps PDFium during format enumeration.
# Use only our bounded, lazy reader, including in build-tree tests. Never edit
# the SDK: give executables a private copy of its plugin tree without qpdf.
get_target_property(ix_qmake Qt6::qmake IMPORTED_LOCATION)
execute_process(COMMAND "${ix_qmake}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE ix_qt_plugins OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT IS_DIRECTORY "${ix_qt_plugins}")
    message(FATAL_ERROR "Cannot locate Qt plugins for the PDF-enabled build")
endif()
file(COPY "${ix_qt_plugins}/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/qt-plugins"
    PATTERN "qpdf*" EXCLUDE PATTERN "libqpdf*" EXCLUDE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qt.conf"
    "[Paths]\nPlugins=qt-plugins\n")
if(APPLE)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/uxzView.app/Contents/Resources")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/uxzView.app/Contents/Resources/qt.conf"
        "[Paths]\nPlugins=${CMAKE_CURRENT_BINARY_DIR}/qt-plugins\n")
endif()
