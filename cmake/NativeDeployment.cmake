# SPDX-License-Identifier: GPL-3.0-or-later
# Quoted generator-expression arguments for qt_deploy_runtime_dependencies.
function(ix_deployment_module_arguments output)
    set(arguments "")
    foreach(module ixrawpreview ixextensions)
        if(NOT TARGET ${module})
            continue()
        endif()
        if(APPLE)
            set(path "uxzView.app/Contents/PlugIns/IxView/$<TARGET_FILE_NAME:${module}>")
        elseif(WIN32)
            set(path "${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:${module}>")
        else()
            # CMake's ELF scanner needs the build RUNPATH to find SDK-only
            # dependencies (e.g. Qt Network, used only by the lazy manager).
            # The installed module already has its relocatable $ORIGIN RUNPATH
            # and cannot resolve those dependencies before they are deployed.
            set(path "$<TARGET_FILE:${module}>")
        endif()
        string(APPEND arguments " \"${path}\"")
    endforeach()
    if(TARGET ixarchive)
        if(APPLE)
            set(path "uxzView.app/Contents/PlugIns/imageformats/$<TARGET_FILE_NAME:ixarchive>")
        elseif(WIN32)
            set(path "${ix_archive_plugin_dir}/$<TARGET_FILE_NAME:ixarchive>")
        else()
            set(path "$<TARGET_FILE:ixarchive>")
        endif()
        string(APPEND arguments " \"${path}\"")
    endif()
    set(${output} "${arguments}" PARENT_SCOPE)
endfunction()
