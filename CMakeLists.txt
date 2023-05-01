#SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2023, Christoph Fritz <chf.fritz@googlemail.com>

cmake_minimum_required(VERSION 3.18)
project(quickchunk VERSION 0.0.2 LANGUAGES C)

set(CMAKE_C_STANDARD 17)

find_package(xxHash CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
message(STATUS "GLIB lib: ${GLIB_LIBRARIES} inc: ${GLIB_INCLUDE_DIRS}")

pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
message(STATUS "GIO lib: ${GIO_LIBRARIES} inc: ${GIO_INCLUDE_DIRS}")

add_executable(quickchunk quickchunk.c quickchunk.h client.c client.h server.c server.h)

target_link_libraries(quickchunk
        PkgConfig::GLIB
        PkgConfig::GIO
        xxHash::xxhash
)

add_compile_definitions(PROJECT_VERSION="${quickchunk_VERSION}")
