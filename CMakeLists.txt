project(kcmsystemd)
set(KCM_SYSTEMD_VERSION_MAJOR "0")
set(KCM_SYSTEMD_VERSION_MINOR "7")
set(KCM_SYSTEMD_VERSION_PATCH "0")
set(KCM_SYSTEMD_VERSION "${KCM_SYSTEMD_VERSION_MAJOR}.${KCM_SYSTEMD_VERSION_MINOR}.${KCM_SYSTEMD_VERSION_PATCH}")

cmake_minimum_required(VERSION 2.6)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

find_package(Qt4 4.6.0 REQUIRED)
find_package(KDE4 4.4.0 REQUIRED)
include(KDE4Defaults)

include_directories( ${QT_INCLUDES} ${CMAKE_CURRENT_BINARY_DIR} )
include_directories( ${KDE4_INCLUDES} )
add_definitions( ${KDE4_DEFINITIONS} )

set(kcmsystemd_INCLUDE_DIRS ${KDE4_INCLUDES})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")

configure_file(config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/src/config.h)

add_subdirectory(other)
add_subdirectory(src)
