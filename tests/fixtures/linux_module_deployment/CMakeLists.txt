# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(OptionalModuleDeploymentFixture LANGUAGES C)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This fixture exercises the native Linux ELF loader")
endif()
add_library(network_fixture SHARED network.c)
set_target_properties(network_fixture PROPERTIES OUTPUT_NAME ixFixtureNetwork SOVERSION 6
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sdk")
add_library(ixextensions MODULE module.c)
set_target_properties(ixextensions PROPERTIES PREFIX "" INSTALL_RPATH "$ORIGIN/..")
target_link_libraries(ixextensions PRIVATE network_fixture)
install(TARGETS ixextensions LIBRARY DESTINATION lib/IxView)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/NativeDeployment.cmake")
ix_deployment_module_arguments(deployment_modules)
# Negative control: the installed module has lost its SDK RUNPATH. Confirm the
# old scan cannot find a dependency that is deliberately absent from the OS.
set(scan [=[
cmake_minimum_required(VERSION 3.24)
file(GET_RUNTIME_DEPENDENCIES
    MODULES "${CMAKE_INSTALL_PREFIX}/lib/IxView/ixextensions.so"
    RESOLVED_DEPENDENCIES_VAR resolved UNRESOLVED_DEPENDENCIES_VAR missing)
if(NOT "libixFixtureNetwork.so.6" IN_LIST missing)
    message(FATAL_ERROR "Negative control did not reproduce the missing dependency")
endif()
]=])
string(APPEND scan "\nfile(GET_RUNTIME_DEPENDENCIES MODULES ${deployment_modules}\n"
    "    RESOLVED_DEPENDENCIES_VAR resolved UNRESOLVED_DEPENDENCIES_VAR missing)\n")
string(APPEND scan [=[
if(missing)
    message(FATAL_ERROR "Build-module deployment has unresolved dependencies: ${missing}")
endif()
file(INSTALL ${resolved} DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" FOLLOW_SYMLINK_CHAIN)
]=])
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/deploy.cmake" CONTENT "${scan}")
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy.cmake")
