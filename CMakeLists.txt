cmake_minimum_required(VERSION 3.24)

project(
  UniVision
  VERSION 0.1.0
  DESCRIPTION "Cross-vendor industrial image acquisition runtime"
  LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(UNIVISION_BUILD_TESTS "Build UniVision tests" ${PROJECT_IS_TOP_LEVEL})
option(UNIVISION_BUILD_EXAMPLES "Build UniVision examples" ${PROJECT_IS_TOP_LEVEL})
option(UNIVISION_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(univision_core
  src/c_api.cpp
  src/simulator.cpp
  src/status.cpp
  src/system.cpp
)
add_library(UniVision::Core ALIAS univision_core)

target_compile_features(univision_core PUBLIC cxx_std_20)
target_include_directories(univision_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(univision_core PRIVATE UNIVISION_BUILDING_LIBRARY)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(univision_core PUBLIC UNIVISION_SHARED)
endif()

if(MSVC)
  target_compile_options(univision_core PRIVATE /W4 /permissive-)
  if(UNIVISION_WARNINGS_AS_ERRORS)
    target_compile_options(univision_core PRIVATE /WX)
  endif()
else()
  target_compile_options(univision_core PRIVATE -Wall -Wextra -Wpedantic)
  if(UNIVISION_WARNINGS_AS_ERRORS)
    target_compile_options(univision_core PRIVATE -Werror)
  endif()
endif()

set_target_properties(univision_core PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  EXPORT_NAME Core
  VISIBILITY_INLINES_HIDDEN YES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(UNIVISION_BUILD_EXAMPLES)
  add_executable(univision_list_devices examples/list_devices.cpp)
  target_link_libraries(univision_list_devices PRIVATE UniVision::Core)
endif()

if(UNIVISION_BUILD_TESTS)
  enable_testing()
  add_executable(univision_core_tests tests/core_tests.cpp)
  target_link_libraries(univision_core_tests PRIVATE UniVision::Core)
  add_test(NAME univision_core_tests COMMAND univision_core_tests)

  add_executable(univision_c_api_tests tests/c_api_tests.c)
  target_link_libraries(univision_c_api_tests PRIVATE UniVision::Core)
  set_target_properties(univision_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
  add_test(NAME univision_c_api_tests COMMAND univision_c_api_tests)
endif()

install(TARGETS univision_core
  EXPORT UniVisionTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/UniVisionConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
  cmake/UniVisionConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/UniVisionConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UniVision
)
install(EXPORT UniVisionTargets
  FILE UniVisionTargets.cmake
  NAMESPACE UniVision::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UniVision
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/UniVisionConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/UniVisionConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UniVision
)
