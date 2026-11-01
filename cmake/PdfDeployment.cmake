# SPDX-License-Identifier: GPL-3.0-or-later
# Invoked after Qt runtime deployment, within the chosen installation prefix.
# PDF rasterization belongs to ixpdf; the generic handler loads PDFium eagerly.
if(NOT IS_ABSOLUTE "${QT_DEPLOY_PREFIX}")
    message(FATAL_ERROR "PDF deployment requires an absolute installation prefix")
endif()
set(ix_pdf_plugin_dirs
    "${QT_DEPLOY_PREFIX}/${QT_DEPLOY_PLUGINS_DIR}/imageformats"
    "${QT_DEPLOY_PREFIX}/uxzView.app/Contents/PlugIns/imageformats")
foreach(plugin_dir IN LISTS ix_pdf_plugin_dirs)
    foreach(name qpdf.dll qpdfd.dll libqpdf.so libqpdf.dylib)
        file(REMOVE "${plugin_dir}/${name}")
    endforeach()
endforeach()
