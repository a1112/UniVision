cmake_minimum_required(VERSION 3.24)

project(
  UniVision
  VERSION 0.3.0
  DESCRIPTION "Cross-vendor industrial image acquisition runtime"
  LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(UNIVISION_BUILD_TESTS "Build UniVision tests" ${PROJECT_IS_TOP_LEVEL})
option(UNIVISION_BUILD_EXAMPLES "Build UniVision examples" ${PROJECT_IS_TOP_LEVEL})
option(UNIVISION_BUILD_GUI "Build the Qt Quick camera debugger (requires Qt 6.5+)" OFF)
option(UNIVISION_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(UNIVISION_ENABLE_CONTRACTS "Build industrial suite data contracts" OFF)
option(UNIVISION_ENABLE_MEASURE "Build UniMeasure offline measurement" OFF)
option(UNIVISION_ENABLE_RECORDING "Build shared MCAP recording" OFF)
option(UNIVISION_ENABLE_REPLAY "Build VisionReplay engine" OFF)
option(UNIVISION_ENABLE_STREAM "Build UniStream bounded delivery and UVS1 codec" OFF)
option(UNIVISION_ENABLE_FLOW "Build InspectFlow offline executor" OFF)
option(UNIVISION_BUILD_UV_CLI "Build industrial suite headless CLI" OFF)

add_library(univision_core
  src/c_api.cpp
  src/genapi/genapi.cpp
  src/gentl.cpp
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
target_link_libraries(univision_core PRIVATE ${CMAKE_DL_LIBS})

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

function(univision_extension_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(UNIVISION_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(UNIVISION_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

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

if(UNIVISION_BUILD_GUI)
  add_subdirectory(apps/camera_debugger)
endif()

if(UNIVISION_BUILD_TESTS)
  enable_testing()
  add_executable(univision_core_tests tests/core_tests.cpp)
  target_link_libraries(univision_core_tests PRIVATE UniVision::Core)
  add_test(NAME univision_core_tests COMMAND univision_core_tests)

  add_executable(univision_simulator_roi_tests tests/simulator_roi_tests.cpp)
  target_link_libraries(univision_simulator_roi_tests PRIVATE UniVision::Core)
  univision_extension_warnings(univision_simulator_roi_tests)
  add_test(NAME univision_simulator_roi_tests COMMAND univision_simulator_roi_tests)

  add_executable(univision_genapi_tests tests/genapi_tests.cpp)
  target_link_libraries(univision_genapi_tests PRIVATE UniVision::Core)
  target_include_directories(univision_genapi_tests PRIVATE src)
  add_test(NAME univision_genapi_tests COMMAND univision_genapi_tests)

  add_executable(univision_c_api_tests tests/c_api_tests.c)
  target_link_libraries(univision_c_api_tests PRIVATE UniVision::Core)
  set_target_properties(univision_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
  add_test(NAME univision_c_api_tests COMMAND univision_c_api_tests)

  add_library(univision_fake_cti SHARED tests/fake_gentl_producer.cpp)
  target_compile_features(univision_fake_cti PRIVATE cxx_std_20)
  target_include_directories(univision_fake_cti PRIVATE src/gentl)
  set_target_properties(univision_fake_cti PROPERTIES
    PREFIX ""
    SUFFIX ".cti"
    CXX_VISIBILITY_PRESET hidden
  )

  add_executable(univision_gentl_tests tests/gentl_tests.cpp)
  target_link_libraries(univision_gentl_tests PRIVATE UniVision::Core)
  add_dependencies(univision_gentl_tests univision_fake_cti)
  add_test(NAME univision_gentl_tests
    COMMAND univision_gentl_tests $<TARGET_FILE:univision_fake_cti>
  )

  add_executable(univision_gentl_c_api_tests tests/gentl_c_api_tests.c)
  target_link_libraries(univision_gentl_c_api_tests PRIVATE UniVision::Core)
  set_target_properties(univision_gentl_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
  add_dependencies(univision_gentl_c_api_tests univision_fake_cti)
  add_test(NAME univision_gentl_c_api_tests
    COMMAND univision_gentl_c_api_tests $<TARGET_FILE:univision_fake_cti>
  )
endif()

if(UNIVISION_ENABLE_CONTRACTS)
  add_library(univision_contracts STATIC modules/contracts/src/contracts.cpp)
  add_library(UniVision::Contracts ALIAS univision_contracts)
  target_compile_features(univision_contracts PUBLIC cxx_std_20)
  target_include_directories(univision_contracts PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/contracts/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(univision_contracts PUBLIC UniVision::Core)
  univision_extension_warnings(univision_contracts)
  set_target_properties(univision_contracts PROPERTIES EXPORT_NAME Contracts)
  if(UNIVISION_BUILD_TESTS)
    add_executable(univision_contracts_tests
      tests/industrial-suite/contracts_tests.cpp)
    target_link_libraries(univision_contracts_tests PRIVATE UniVision::Contracts)
    add_test(NAME univision_contracts_tests COMMAND univision_contracts_tests)
  endif()
  install(TARGETS univision_contracts EXPORT UniVisionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY modules/contracts/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(UNIVISION_ENABLE_MEASURE)
  if(NOT UNIVISION_ENABLE_CONTRACTS)
    message(FATAL_ERROR "UNIVISION_ENABLE_MEASURE requires UNIVISION_ENABLE_CONTRACTS")
  endif()
  add_library(univision_measure STATIC modules/measure/src/measure.cpp)
  add_library(UniVision::Measure ALIAS univision_measure)
  target_compile_features(univision_measure PUBLIC cxx_std_20)
  target_include_directories(univision_measure PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/measure/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(univision_measure PUBLIC UniVision::Contracts)
  univision_extension_warnings(univision_measure)
  set_target_properties(univision_measure PROPERTIES EXPORT_NAME Measure)
  if(UNIVISION_BUILD_TESTS)
    add_executable(univision_measure_tests tests/industrial-suite/measure_tests.cpp)
    target_link_libraries(univision_measure_tests PRIVATE UniVision::Measure)
    add_test(NAME univision_measure_tests COMMAND univision_measure_tests)
  endif()
  install(TARGETS univision_measure EXPORT UniVisionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY modules/measure/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(UNIVISION_ENABLE_RECORDING)
  if(NOT UNIVISION_ENABLE_CONTRACTS)
    message(FATAL_ERROR "UNIVISION_ENABLE_RECORDING requires UNIVISION_ENABLE_CONTRACTS")
  endif()
  include(FetchContent)
  FetchContent_Declare(univision_mcap
    URL https://codeload.github.com/foxglove/mcap/tar.gz/1420296ffcfdcde4b6894c0c1aba0ad083f93dde
    URL_HASH SHA256=f72059d1e926563b04500e51d3f89b4f590c7d52eff3205b191705ff4ec62050
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(univision_mcap)
  FetchContent_Declare(univision_json
    URL https://codeload.github.com/nlohmann/json/tar.gz/65ee68451d8eb2b5f3a30b410476ab83deb3289b
    URL_HASH SHA256=13ef31d691947940a08909f8e0772f1d7d68e5da1678ee812a49c4bb0c996b2f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR cmake-do-not-add)
  FetchContent_MakeAvailable(univision_json)
  add_library(univision_recording STATIC modules/recording/src/recording.cpp)
  add_library(UniVision::Recording ALIAS univision_recording)
  target_compile_features(univision_recording PUBLIC cxx_std_20)
  target_include_directories(univision_recording PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/recording/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_include_directories(univision_recording SYSTEM PRIVATE
    ${univision_mcap_SOURCE_DIR}/cpp/mcap/include
    ${univision_json_SOURCE_DIR}/single_include
  )
  target_link_libraries(univision_recording PUBLIC UniVision::Contracts)
  univision_extension_warnings(univision_recording)
  set_target_properties(univision_recording PROPERTIES EXPORT_NAME Recording)
  if(UNIVISION_BUILD_TESTS)
    add_executable(univision_recording_tests
      tests/industrial-suite/recording_tests.cpp)
    target_link_libraries(univision_recording_tests PRIVATE UniVision::Recording)
    add_test(NAME univision_recording_tests COMMAND univision_recording_tests)
  endif()
  install(TARGETS univision_recording EXPORT UniVisionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY modules/recording/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(UNIVISION_ENABLE_REPLAY)
  if(NOT UNIVISION_ENABLE_RECORDING)
    message(FATAL_ERROR "UNIVISION_ENABLE_REPLAY requires UNIVISION_ENABLE_RECORDING")
  endif()
  add_library(univision_replay STATIC modules/replay/src/replay.cpp)
  add_library(UniVision::Replay ALIAS univision_replay)
  target_compile_features(univision_replay PUBLIC cxx_std_20)
  target_include_directories(univision_replay PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/replay/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(univision_replay PUBLIC UniVision::Recording)
  univision_extension_warnings(univision_replay)
  set_target_properties(univision_replay PROPERTIES EXPORT_NAME Replay)
  if(UNIVISION_BUILD_TESTS)
    add_executable(univision_replay_tests tests/industrial-suite/replay_tests.cpp)
    target_link_libraries(univision_replay_tests PRIVATE UniVision::Replay)
    add_test(NAME univision_replay_tests COMMAND univision_replay_tests)
  endif()
  install(TARGETS univision_replay EXPORT UniVisionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY modules/replay/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(UNIVISION_ENABLE_STREAM)
  if(NOT UNIVISION_ENABLE_CONTRACTS)
    message(FATAL_ERROR "UNIVISION_ENABLE_STREAM requires UNIVISION_ENABLE_CONTRACTS")
  endif()
  add_library(univision_stream STATIC modules/stream/src/stream.cpp)
  add_library(UniVision::Stream ALIAS univision_stream)
  target_compile_features(univision_stream PUBLIC cxx_std_20)
  target_include_directories(univision_stream PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/stream/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(univision_stream PUBLIC UniVision::Contracts)
  univision_extension_warnings(univision_stream)
  set_target_properties(univision_stream PROPERTIES EXPORT_NAME Stream)
  if(UNIVISION_BUILD_TESTS)
    add_executable(univision_stream_tests tests/industrial-suite/stream_tests.cpp)
    target_link_libraries(univision_stream_tests PRIVATE UniVision::Stream)
    add_test(NAME univision_stream_tests COMMAND univision_stream_tests)
  endif()
  install(TARGETS univision_stream EXPORT UniVisionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY modules/stream/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(UNIVISION_ENABLE_FLOW)
  if(NOT UNIVISION_ENABLE_MEASURE OR NOT UNIVISION_ENABLE_RECORDING)
    message(FATAL_ERROR "UNIVISION_ENABLE_FLOW requires MEASURE and RECORDING")
  endif()
  add_library(univision_flow STATIC modules/flow/src/flow.cpp)
  add_library(UniVision::Flow ALIAS univision_flow)
  target_compile_features(univision_flow PUBLIC cxx_std_20)
  target_include_directories(univision_flow PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/flow/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_include_directories(univision_flow SYSTEM PRIVATE
    ${univision_json_SOURCE_DIR}/single_include)
  target_link_libraries(univision_flow PUBLIC
    UniVision::Measure UniVision::Recording)
  univision_extension_warnings(univision_flow)
  set_target_properties(univision_flow PROPERTIES EXPORT_NAME Flow)
  if(UNIVISION_BUILD_TESTS)
    add_executable(univision_flow_tests tests/industrial-suite/flow_tests.cpp)
    target_link_libraries(univision_flow_tests PRIVATE UniVision::Flow)
    target_compile_definitions(univision_flow_tests PRIVATE
      UV_TEST_PROFILE="${CMAKE_CURRENT_SOURCE_DIR}/docs/design/industrial-suite/examples/data/profile-circle.csv"
      UV_TEST_FLOW="${CMAKE_CURRENT_SOURCE_DIR}/docs/design/industrial-suite/examples/flow-definition.json")
    add_test(NAME univision_flow_tests COMMAND univision_flow_tests)
  endif()
  install(TARGETS univision_flow EXPORT UniVisionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY modules/flow/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(UNIVISION_BUILD_UV_CLI)
  if(NOT UNIVISION_ENABLE_STREAM OR NOT UNIVISION_ENABLE_REPLAY OR
     NOT UNIVISION_ENABLE_MEASURE OR NOT UNIVISION_ENABLE_FLOW)
    message(FATAL_ERROR "UNIVISION_BUILD_UV_CLI requires all industrial modules")
  endif()
  add_executable(univision_uv_cli apps/uv-cli/main.cpp)
  target_link_libraries(univision_uv_cli PRIVATE
    UniVision::Stream UniVision::Replay UniVision::Flow)
  univision_extension_warnings(univision_uv_cli)
  install(TARGETS univision_uv_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  if(UNIVISION_BUILD_TESTS)
    add_test(NAME univision_cli_demo
      COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:univision_uv_cli>
        -DWORKSPACE=${CMAKE_CURRENT_BINARY_DIR}
        -DFLOW=${CMAKE_CURRENT_SOURCE_DIR}/docs/design/industrial-suite/examples/flow-definition.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/industrial-suite/check_cli.cmake)
  endif()
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
