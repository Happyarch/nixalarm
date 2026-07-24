cmake_minimum_required(VERSION 3.20)
project(nixalarm VERSION 0.2.0 LANGUAGES CXX)

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
pkg_check_modules(RSVG REQUIRED IMPORTED_TARGET librsvg-2.0)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

add_library(dial_math STATIC
  src/astro.cpp
  src/dial_geometry.cpp
  src/dial_scene.cpp
  src/dial_engrave.cpp
)
target_include_directories(dial_math PUBLIC src third_party)
target_compile_options(dial_math PRIVATE -Wall -Wextra -Wpedantic)

# The raytraced OpenGL rendering backend for the sundial/moondial ClockFace
# faces. Split out from dial_math because dial_scene.cpp (analytic scene
# construction) has no GL/SDL dependency and is unit-tested without a
# window/context, while this library owns the actual GL context and the
# fragment-shader ray tracer plus the two ClockFace subclasses that drive it.
add_library(dial_render STATIC
  src/dial_gl.cpp
  src/dial_clock.cpp
  src/sundial.cpp
  src/moondial.cpp
  src/gl_util.cpp
  src/procedural_texture.cpp
)
target_include_directories(dial_render PUBLIC src third_party)
target_compile_options(dial_render PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(dial_render PRIVATE
  NIXALARM_DATADIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/nixalarm")
target_link_libraries(dial_render PUBLIC dial_math PkgConfig::SDL2 PkgConfig::EPOXY)

add_library(analog_clock STATIC
  src/analog.cpp
)
target_include_directories(analog_clock PUBLIC src third_party)
target_compile_options(analog_clock PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(analog_clock PRIVATE
  NIXALARM_DATADIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/nixalarm")
target_link_libraries(analog_clock PUBLIC PkgConfig::SDL2 PkgConfig::RSVG)

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
target_link_libraries(nixalarm PRIVATE dial_math dial_render analog_clock)
# --version/--help print PROJECT_VERSION rather than their own copy of it, so
# the one in project() above is the single place a release bump has to touch.
target_compile_definitions(nixalarm PRIVATE
  NIXALARM_DATADIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/nixalarm"
  NIXALARM_VERSION="${PROJECT_VERSION}")
target_link_libraries(nixalarm PRIVATE
  PkgConfig::SDL2
  PkgConfig::AVFORMAT
  PkgConfig::AVCODEC
  PkgConfig::AVUTIL
  PkgConfig::SWRESAMPLE
  PkgConfig::FLUIDSYNTH
)

enable_testing()
add_executable(nixalarm_dial_tests tests/test_dial_geometry.cpp)
target_link_libraries(nixalarm_dial_tests PRIVATE dial_math)
add_test(NAME dial_geometry_tests COMMAND nixalarm_dial_tests)

add_executable(nixalarm_dial_scene_tests tests/test_dial_scene.cpp)
target_link_libraries(nixalarm_dial_scene_tests PRIVATE dial_math)
add_test(NAME dial_scene_tests COMMAND nixalarm_dial_scene_tests)

add_executable(nixalarm_dial_engrave_tests tests/test_dial_engrave.cpp)
target_link_libraries(nixalarm_dial_engrave_tests PRIVATE dial_math)
add_test(NAME dial_engrave_tests COMMAND nixalarm_dial_engrave_tests)

add_executable(nixalarm_analog_tests tests/test_analog.cpp)
target_link_libraries(nixalarm_analog_tests PRIVATE analog_clock)
add_test(NAME analog_tests COMMAND nixalarm_analog_tests)

install(TARGETS nixalarm RUNTIME DESTINATION bin)
install(DIRECTORY assets/runtime/nixie
        DESTINATION ${CMAKE_INSTALL_DATADIR}/nixalarm
        FILES_MATCHING PATTERN "*.png" PATTERN "*.json")
# HH:MM (4-tube) nixie layout, used when show_seconds = false. Still being authored,
# so it's not guaranteed to exist in every checkout -- install it opportunistically
# rather than failing configure/packaging when it's absent.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/runtime/nixie-hhmm")
  install(DIRECTORY assets/runtime/nixie-hhmm
          DESTINATION ${CMAKE_INSTALL_DATADIR}/nixalarm
          FILES_MATCHING PATTERN "*.png" PATTERN "*.json")
endif()
install(DIRECTORY assets/runtime/analog
        DESTINATION ${CMAKE_INSTALL_DATADIR}/nixalarm
        FILES_MATCHING PATTERN "*.svg")
install(DIRECTORY assets/runtime/dial
        DESTINATION ${CMAKE_INSTALL_DATADIR}/nixalarm
        FILES_MATCHING PATTERN "*.png")
install(FILES assets/runtime/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/nixalarm)
install(FILES data/icons/hicolor/scalable/apps/nixalarm.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
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
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl2-2.0-0, ffmpeg, libfluidsynth3, librsvg2-2, libepoxy0")
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
endif()
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_REQUIRES "SDL2, ffmpeg, fluidsynth-libs, librsvg2, libepoxy")
include(CPack)
