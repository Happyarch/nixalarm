cmake_minimum_required(VERSION 3.20)
project(nixalarm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)

add_executable(nixalarm src/main.cpp)
target_compile_options(nixalarm PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(nixalarm PRIVATE
  PkgConfig::SDL2
  PkgConfig::AVFORMAT
  PkgConfig::AVCODEC
  PkgConfig::AVUTIL
  PkgConfig::SWRESAMPLE
)

install(TARGETS nixalarm RUNTIME DESTINATION bin)
