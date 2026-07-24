cmake_minimum_required(VERSION 3.20)
project(nixalarm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)

add_executable(nixalarm
  src/main.cpp
  src/config.cpp
  src/audio.cpp
  src/clock.cpp
  src/nixie.cpp
  src/stb_image_impl.cpp
  src/alarm.cpp
)
target_include_directories(nixalarm PRIVATE src third_party)
target_compile_options(nixalarm PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(nixalarm PRIVATE
  NIXALARM_DATADIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/nixalarm")
target_link_libraries(nixalarm PRIVATE
  PkgConfig::SDL2
  PkgConfig::AVFORMAT
  PkgConfig::AVCODEC
  PkgConfig::AVUTIL
  PkgConfig::SWRESAMPLE
  PkgConfig::FLUIDSYNTH
)

install(TARGETS nixalarm RUNTIME DESTINATION bin)
install(DIRECTORY assets/runtime/nixie
        DESTINATION ${CMAKE_INSTALL_DATADIR}/nixalarm
        FILES_MATCHING PATTERN "*.png" PATTERN "*.json")
install(FILES docs/man/nixalarm.1 DESTINATION share/man/man1)
install(FILES docs/man/nixalarm.5 DESTINATION share/man/man5)
install(FILES data/nixalarm.desktop DESTINATION share/applications)
install(FILES LICENSE DESTINATION share/licenses/nixalarm)

set(CPACK_PACKAGE_NAME "nixalarm")
set(CPACK_PACKAGE_VENDOR "Happyarch")
set(CPACK_PACKAGE_CONTACT "Happyarch")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Seven-segment Linux alarm clock")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Happyarch/nixalarm")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "DEB;RPM")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Happyarch")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl2-2.0-0, ffmpeg, libfluidsynth3")
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
endif()
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_REQUIRES "SDL2, ffmpeg, fluidsynth-libs")
include(CPack)
