cmake_minimum_required(VERSION 3.24)
project(UniVisionIndustrialConsumer LANGUAGES CXX)
find_package(UniVision CONFIG REQUIRED)
add_executable(industrial_consumer main.cpp)
target_link_libraries(industrial_consumer PRIVATE
  UniVision::Flow UniVision::Replay UniVision::Stream)
enable_testing()
add_test(NAME industrial_consumer COMMAND industrial_consumer)
