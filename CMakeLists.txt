cmake_minimum_required(VERSION 3.24)
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum target macOS version")
endif()
project(IxView VERSION 0.5.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
include(GNUInstallDirs)
include(CTest)
if(MSVC)
    add_compile_options(/utf-8)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent PrintSupport Svg)
qt_standard_project_setup()
include(cmake/Dependencies.cmake)
option(IX_REQUIRE_FULL_FEATURES "Fail if metadata, RAW previews or the extension manager are unavailable" OFF)
add_library(ixcore STATIC
    src/core/image_document.cpp src/core/image_memory.cpp src/core/document_history.cpp
    src/core/codec_registry.cpp src/core/processing.cpp src/core/localization.cpp
    src/core/resize_model.cpp src/core/batch_processor.cpp src/core/metadata.cpp src/core/file_revision.cpp
    src/core/jpeg_transform.cpp src/core/apng_codec.cpp src/core/rotation.cpp src/core/selection.cpp src/core/directory_index.cpp src/core/preview_service.cpp
    src/core/file_operations.cpp src/core/image_catalog.cpp src/core/batch_pipeline.cpp src/core/batch_plan.cpp src/core/batch_report.cpp src/core/plugin_store.cpp
    src/core/app_storage.cpp src/core/extension_storage.cpp src/core/tool_store.cpp src/core/recognition_input.cpp
    src/core/image_loading.cpp src/core/directory_index_service.cpp)
target_include_directories(ixcore PUBLIC src)
target_link_libraries(ixcore PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent PRIVATE IxView::TurboJPEG)
if(TARGET IxView::Exiv2)
    target_link_libraries(ixcore PRIVATE IxView::Exiv2)
    target_compile_definitions(ixcore PRIVATE IX_HAVE_EXIV2)
endif()
add_library(ixui STATIC src/ui/appearance.cpp src/ui/interface_language.cpp src/ui/image_view.cpp src/ui/fullscreen_info.cpp src/ui/resize_dialog.cpp src/ui/dialogs.cpp
    src/ui/main_window.cpp src/ui/main_window_files.cpp src/ui/slideshow_controller.cpp src/ui/rotation_dialog.cpp src/ui/application.cpp src/ui/platform_support.cpp src/ui/preview_strip.cpp src/ui/action_search.cpp src/ui/pipeline_dialog.cpp src/ui/pipeline_step_editor.cpp src/ui/pipeline_result_model.cpp src/ui/library_dialog.cpp src/ui/icons.cpp src/ui/settings_dialog.cpp src/ui/tool_dialog.cpp src/ui/crop_ratio_editor.cpp src/ui/extension_dialog.cpp)
target_link_libraries(ixui PUBLIC ixcore Qt6::Widgets Qt6::PrintSupport Qt6::Svg)
target_compile_definitions(ixui PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
qt_add_executable(IxView WIN32 MACOSX_BUNDLE src/main.cpp resources/app.qrc)
target_compile_definitions(IxView PRIVATE IXVIEW_VERSION="${PROJECT_VERSION}")
if(WIN32)
    enable_language(RC)
    configure_file(resources/windows/app.rc.in uxzView.rc @ONLY)
    if(MINGW)
        # This icon-only resource needs no Qt includes/defines. Avoid windres'
        # broken quoting of full preprocessor paths and inherited include lists.
        get_filename_component(ix_rc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(ix_rc_cpp "${CMAKE_CXX_COMPILER}" NAME)
        if(CMAKE_HOST_WIN32)
            set(ix_rc_path "${ix_rc_bin};$ENV{PATH}")
        else()
            set(ix_rc_path "${ix_rc_bin}:$ENV{PATH}")
        endif()
        set(ix_icon_object "${CMAKE_CURRENT_BINARY_DIR}/uxzView-icon.o")
        add_custom_command(OUTPUT "${ix_icon_object}"
            COMMAND "${CMAKE_COMMAND}" -E env "PATH=${ix_rc_path}" "${CMAKE_RC_COMPILER}"
                "--preprocessor=${ix_rc_cpp}" --preprocessor-arg=-E --preprocessor-arg=-xc
                --preprocessor-arg=-DRC_INVOKED -O coff
                -i "${CMAKE_CURRENT_BINARY_DIR}/uxzView.rc" -o "${ix_icon_object}"
            DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/uxzView.rc" resources/uxzView.ico VERBATIM)
        set_source_files_properties("${ix_icon_object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
        target_sources(IxView PRIVATE "${ix_icon_object}")
    else()
        target_sources(IxView PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/uxzView.rc")
    endif()
elseif(APPLE)
    target_sources(IxView PRIVATE resources/uxzView.icns)
    set_source_files_properties(resources/uxzView.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(IxView PROPERTIES MACOSX_BUNDLE_ICON_FILE uxzView.icns)
endif()
target_link_libraries(IxView PRIVATE ixui)

# Optional image archives use the KArchive library already shipped with KRA/ORA
# in the Windows SDK. Keep it out of the viewer's direct imports/startup path.
option(IX_ARCHIVE_FORMATS "Build the optional read-only ZIP/CBZ image plugin when KArchive is available" ON)
if(IX_ARCHIVE_FORMATS)
    find_package(KF6Archive QUIET)
    if(TARGET KF6::Archive)
        add_library(ixarchive MODULE src/modules/archive_image.cpp)
        set_target_properties(ixarchive PROPERTIES PREFIX ""
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/imageformats"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/imageformats")
        target_link_libraries(ixarchive PRIVATE Qt6::Gui KF6::Archive)
        add_dependencies(ixcore ixarchive)
        if(APPLE)
            set_target_properties(ixarchive PROPERTIES INSTALL_RPATH "@loader_path/../../Frameworks")
            set(ix_archive_plugin_dir "uxzView.app/Contents/PlugIns/imageformats")
        else()
            # Also discoverable with system Qt, without a deployment qt.conf.
            set(ix_archive_plugin_dir "${CMAKE_INSTALL_BINDIR}/imageformats")
            file(RELATIVE_PATH ix_archive_lib_relative "/prefix/${ix_archive_plugin_dir}" "/prefix/${CMAKE_INSTALL_LIBDIR}")
            set_target_properties(ixarchive PROPERTIES INSTALL_RPATH "$ORIGIN/${ix_archive_lib_relative}")
        endif()
        install(TARGETS ixarchive LIBRARY DESTINATION "${ix_archive_plugin_dir}"
            RUNTIME DESTINATION "${ix_archive_plugin_dir}")
    else()
        message(STATUS "ZIP/CBZ image plugin omitted: optional KF6Archive development package not found")
    endif()
endif()

if(NOT TARGET ixarchive)
    # Qt discovers plugins by directory, including stale output from a previous
    # configuration. Remove only this generated module when switching it off.
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/imageformats/ixarchive${CMAKE_SHARED_MODULE_SUFFIX}")
endif()
# RAW preview is a lazy optional module; ordinary image viewing never maps LibRaw.
find_path(IX_LIBRAW_INCLUDE libraw/libraw.h)
find_library(IX_LIBRAW_LIBRARY NAMES raw_r)
if(IX_LIBRAW_INCLUDE AND IX_LIBRAW_LIBRARY)
    add_library(ixrawpreview MODULE src/modules/raw_preview.cpp)
    set_target_properties(ixrawpreview PROPERTIES PREFIX "")
    target_include_directories(ixrawpreview PRIVATE ${IX_LIBRAW_INCLUDE})
    target_link_libraries(ixrawpreview PRIVATE Qt6::Core Qt6::Gui ${IX_LIBRAW_LIBRARY})
    target_compile_definitions(ixcore PRIVATE IX_RAW_PREVIEW_MODULE="$<TARGET_FILE_NAME:ixrawpreview>")
    target_compile_definitions(IxView PRIVATE IX_RAW_PREVIEW_MODULE="$<TARGET_FILE_NAME:ixrawpreview>")
    add_dependencies(IxView ixrawpreview)
endif()
# Network and its dependencies are mapped only when the manager is opened.
find_package(Qt6 6.8 QUIET COMPONENTS Network)
if(TARGET Qt6::Network)
    add_library(ixextensions MODULE src/modules/extension_manager.cpp)
    set_target_properties(ixextensions PROPERTIES PREFIX "")
    target_link_libraries(ixextensions PRIVATE ixcore Qt6::Widgets Qt6::Network)
    target_compile_definitions(ixui PRIVATE IX_EXTENSIONS_MODULE="$<TARGET_FILE_NAME:ixextensions>")
    target_compile_definitions(IxView PRIVATE IX_EXTENSIONS_MODULE="$<TARGET_FILE_NAME:ixextensions>")
    add_dependencies(IxView ixextensions)
endif()
if(IX_REQUIRE_FULL_FEATURES)
    foreach(feature IxView::Exiv2 ixrawpreview ixextensions)
        if(NOT TARGET ${feature})
            message(FATAL_ERROR "Full build requires ${feature}; install the matching development dependency or disable IX_REQUIRE_FULL_FEATURES for a reduced build")
        endif()
    endforeach()
endif()
foreach(module ixrawpreview ixextensions)
    if(TARGET ${module})
        if(APPLE)
            set_target_properties(${module} PROPERTIES INSTALL_RPATH "@loader_path/../../Frameworks")
            install(TARGETS ${module} LIBRARY DESTINATION "uxzView.app/Contents/PlugIns/IxView")
        elseif(WIN32)
            install(TARGETS ${module} LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        else()
            set_target_properties(${module} PROPERTIES INSTALL_RPATH "$ORIGIN/..")
            install(TARGETS ${module} LIBRARY DESTINATION "lib/IxView")
        endif()
    endif()
endforeach()
set_target_properties(IxView PROPERTIES
    OUTPUT_NAME "uxzView"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.ixview.IxView"
    MACOSX_BUNDLE_BUNDLE_NAME "üxzView"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in")
foreach(target ixcore ixui IxView)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    find_package(TIFF REQUIRED)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/output)
    qt_add_executable(ixview_benchmarks tests/bench_ixview.cpp resources/app.qrc)
    target_link_libraries(ixview_benchmarks PRIVATE ixui Qt6::Test)
    qt_add_executable(ixview_tests tests/test_ixview.cpp resources/app.qrc)
    target_link_libraries(ixview_tests PRIVATE ixui Qt6::Test TIFF::TIFF)
    if(TARGET ixarchive)
        target_link_libraries(ixview_tests PRIVATE KF6::Archive)
        target_compile_definitions(ixview_tests PRIVATE IX_TEST_ARCHIVE)
    endif()
    target_compile_definitions(ixview_tests PRIVATE IX_TEST_OUTPUT="${CMAKE_CURRENT_SOURCE_DIR}/tests/output")
    foreach(module ixrawpreview ixextensions)
        if(TARGET ${module})
            add_dependencies(ixview_tests ${module})
            add_dependencies(ixview_benchmarks ${module})
        endif()
    endforeach()
    if(TARGET ixextensions)
        target_compile_definitions(ixview_tests PRIVATE IX_TEST_EXTENSIONS_MODULE="$<TARGET_FILE_NAME:ixextensions>")
    endif()
    if(TARGET IxView::Exiv2)
        target_link_libraries(ixview_tests PRIVATE IxView::Exiv2)
        target_compile_definitions(ixview_tests PRIVATE IX_TEST_EXIV2)
    endif()
    add_test(NAME ixview_tests COMMAND ixview_tests -platform offscreen
        -o ${CMAKE_CURRENT_BINARY_DIR}/test-results.txt,txt
        -o ${CMAKE_CURRENT_BINARY_DIR}/test-results.xml,junitxml)
    qt_add_executable(ixview_workflow_tests tests/workflow_ixview.cpp resources/app.qrc)
    target_link_libraries(ixview_workflow_tests PRIVATE ixui Qt6::Test)
    target_compile_definitions(ixview_workflow_tests PRIVATE
        IX_WORKFLOW_OUTPUT="${CMAKE_CURRENT_SOURCE_DIR}/tests/output"
        IX_WORKFLOW_RAW_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/build/quality-fixtures")
    add_test(NAME ixview_workflow_tests COMMAND ixview_workflow_tests -platform offscreen
        -o ${CMAKE_CURRENT_BINARY_DIR}/workflow-test-results.txt,txt
        -o ${CMAKE_CURRENT_BINARY_DIR}/workflow-test-results.xml,junitxml)
    qt_add_executable(ixview_quality_tests tests/quality_ixview.cpp resources/app.qrc)
    target_link_libraries(ixview_quality_tests PRIVATE ixui Qt6::Test)
    target_compile_definitions(ixview_quality_tests PRIVATE
        IX_QUALITY_OUTPUT="${CMAKE_CURRENT_SOURCE_DIR}/tests/output"
        IX_RAW_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/raw-manifest.json"
        IX_RAW_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/build/quality-fixtures")
    find_path(IX_LCMS_INCLUDE lcms2.h)
    find_library(IX_LCMS_LIBRARY NAMES lcms2)
    if(IX_LCMS_INCLUDE AND IX_LCMS_LIBRARY)
        target_include_directories(ixview_quality_tests PRIVATE ${IX_LCMS_INCLUDE})
        target_link_libraries(ixview_quality_tests PRIVATE ${IX_LCMS_LIBRARY})
        target_compile_definitions(ixview_quality_tests PRIVATE IX_QUALITY_LCMS)
    endif()
    foreach(module ixrawpreview ixextensions)
        if(TARGET ${module})
            add_dependencies(ixview_quality_tests ${module})
        endif()
    endforeach()
    add_test(NAME ixview_quality_tests COMMAND ixview_quality_tests -platform offscreen
        -o ${CMAKE_CURRENT_BINARY_DIR}/quality-test-results.txt,txt
        -o ${CMAKE_CURRENT_BINARY_DIR}/quality-test-results.xml,junitxml)
    qt_add_executable(ixview_storage_tests tests/storage_ixview.cpp)
    target_link_libraries(ixview_storage_tests PRIVATE ixcore Qt6::Test)
    target_compile_definitions(ixview_storage_tests PRIVATE
        IX_STORAGE_OUTPUT="${CMAKE_CURRENT_SOURCE_DIR}/tests/output")
    add_test(NAME ixview_storage_tests COMMAND ixview_storage_tests
        -o ${CMAKE_CURRENT_BINARY_DIR}/storage-test-results.txt,txt
        -o ${CMAKE_CURRENT_BINARY_DIR}/storage-test-results.xml,junitxml)
    qt_add_executable(ixview_file_operation_tests tests/file_operations_ixview.cpp)
    target_link_libraries(ixview_file_operation_tests PRIVATE ixcore Qt6::Test)
    target_compile_definitions(ixview_file_operation_tests PRIVATE IX_FILE_OPERATION_OUTPUT="${CMAKE_CURRENT_SOURCE_DIR}/tests/output")
    add_test(NAME ixview_file_operation_tests COMMAND ixview_file_operation_tests
        -o ${CMAKE_CURRENT_BINARY_DIR}/file-operation-test-results.txt,txt
        -o ${CMAKE_CURRENT_BINARY_DIR}/file-operation-test-results.xml,junitxml)
    qt_add_executable(ixview_extension_tests tests/extensions_ixview.cpp resources/app.qrc)
    target_link_libraries(ixview_extension_tests PRIVATE ixui Qt6::Test)
    target_compile_definitions(ixview_extension_tests PRIVATE
        IX_EXTENSION_OUTPUT="${CMAKE_CURRENT_SOURCE_DIR}/tests/output"
        IX_TOOL_PACKS="${CMAKE_CURRENT_SOURCE_DIR}/dist/tools")
    if(TARGET ixextensions)
        add_dependencies(ixview_extension_tests ixextensions)
        target_compile_definitions(ixview_extension_tests PRIVATE IX_TEST_EXTENSIONS_MODULE="$<TARGET_FILE_NAME:ixextensions>")
    endif()
    add_test(NAME ixview_extension_tests COMMAND ixview_extension_tests -platform offscreen
        -o ${CMAKE_CURRENT_BINARY_DIR}/extension-test-results.txt,txt
        -o ${CMAKE_CURRENT_BINARY_DIR}/extension-test-results.xml,junitxml)
    # Suites share GUI/profile state. Honor this even when a caller uses ctest -j.
    qt_add_executable(ixview_maintenance_tests tests/maintenance_ixview.cpp resources/app.qrc)
    target_link_libraries(ixview_maintenance_tests PRIVATE ixui Qt6::Test)
    if(TARGET ixarchive)
        target_link_libraries(ixview_maintenance_tests PRIVATE KF6::Archive)
        target_compile_definitions(ixview_maintenance_tests PRIVATE IX_TEST_ARCHIVE)
    endif()
    add_test(NAME ixview_maintenance_tests COMMAND ixview_maintenance_tests -platform offscreen
        -o ${CMAKE_CURRENT_BINARY_DIR}/maintenance-test-results.txt,txt
        -o ${CMAKE_CURRENT_BINARY_DIR}/maintenance-test-results.xml,junitxml)
    # A blocked dialog must fail with retained logs rather than occupy CI for hours.
    qt_add_executable(ixview_ui_quality_tests tests/ui_quality_ixview.cpp resources/app.qrc)
    target_link_libraries(ixview_ui_quality_tests PRIVATE ixui Qt6::Test)
    target_compile_definitions(ixview_ui_quality_tests PRIVATE IX_UI_OUTPUT="${CMAKE_CURRENT_SOURCE_DIR}/tests/output")
    if(TARGET ixextensions)
        add_dependencies(ixview_ui_quality_tests ixextensions)
        target_compile_definitions(ixview_ui_quality_tests PRIVATE IX_TEST_EXTENSION_MANAGER)
    endif()
    add_test(NAME ixview_ui_quality_tests COMMAND ixview_ui_quality_tests -platform offscreen
        -o ${CMAKE_CURRENT_BINARY_DIR}/ui-quality-test-results.txt,txt
        -o ${CMAKE_CURRENT_BINARY_DIR}/ui-quality-test-results.xml,junitxml)
    set_tests_properties(ixview_tests ixview_workflow_tests ixview_quality_tests
        ixview_storage_tests ixview_file_operation_tests ixview_extension_tests ixview_maintenance_tests
        ixview_ui_quality_tests
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
endif()
# Standard install layout supports Qt's native deployment tools on all desktop hosts.
install(TARGETS IxView BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32)
    install(DIRECTORY scripts/windows/ DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(APPLE)
    set(IX_DOC_DIR "uxzView.app/Contents/Resources/docs")
else()
    set(IX_DOC_DIR "${CMAKE_INSTALL_DATADIR}/IxView")
endif()
install(FILES LICENSE README.md PROJECT_STATUS.md CHANGELOG.md CONTRIBUTING.md SECURITY.md
    ügz.jpg example.jpg
    DESTINATION ${IX_DOC_DIR})
install(DIRECTORY docs/ DESTINATION "${IX_DOC_DIR}/docs"
    FILES_MATCHING PATTERN "*.md" PATTERN "WORK_IN_PROGRESS.md" EXCLUDE)
if(UNIX AND NOT APPLE)
    install(FILES resources/linux/org.ixview.IxView.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/ixview.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME org.ixview.IxView.svg)
endif()
option(IX_DEPLOY_QT "Bundle runtime libraries/plugins during native cmake install" OFF)
if(IX_DEPLOY_QT)
    include(cmake/NativeDeployment.cmake)
    ix_deployment_module_arguments(ix_deploy_modules)
    if(APPLE)
        set(ix_deploy_executable "uxzView.app")
        set(ix_deploy_conf "")
    else()
        set(ix_deploy_executable "$<TARGET_FILE:IxView>")
        set(ix_deploy_conf "GENERATE_QT_CONF")
    endif()
    qt_generate_deploy_script(TARGET IxView OUTPUT_SCRIPT ix_deploy_script CONTENT
        "qt_deploy_runtime_dependencies(EXECUTABLE \"${ix_deploy_executable}\" ${ix_deploy_conf} ADDITIONAL_MODULES ${ix_deploy_modules})")
    install(SCRIPT ${ix_deploy_script})
endif()
set(CPACK_PACKAGE_NAME "uxzView")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "IxView contributors")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif()
include(CPack)
