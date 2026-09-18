find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)
qt_add_executable(univision_camera_debugger main.cpp camera_controller.cpp camera_controller.h)
qt_add_qml_module(univision_camera_debugger
  URI UniVision.Debugger
  VERSION 1.0
  QML_FILES Main.qml
)
target_link_libraries(univision_camera_debugger PRIVATE UniVision::Core Qt6::Quick Qt6::QuickControls2)
if(MSVC)
  target_compile_options(univision_camera_debugger PRIVATE /utf-8)
endif()
install(TARGETS univision_camera_debugger RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
qt_generate_deploy_qml_app_script(TARGET univision_camera_debugger OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
