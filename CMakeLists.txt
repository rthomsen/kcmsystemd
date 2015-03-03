project(kcmsystemd)
set(KCM_SYSTEMD_VERSION_MAJOR "1")
set(KCM_SYSTEMD_VERSION_MINOR "1")
set(KCM_SYSTEMD_VERSION_PATCH "0")
set(KCM_SYSTEMD_VERSION "${KCM_SYSTEMD_VERSION_MAJOR}.${KCM_SYSTEMD_VERSION_MINOR}.${KCM_SYSTEMD_VERSION_PATCH}")

cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)

# Silence a warning
cmake_policy(SET CMP0037 OLD)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5 5.0.0 CONFIG REQUIRED COMPONENTS
             Widgets
             DBus)

find_package(KF5 REQUIRED COMPONENTS
             CoreAddons
             WidgetsAddons
             ConfigWidgets
             I18n
             Service
             Auth)

message(STATUS "Searching for libsystemd...")
find_library(SYSTEMD_FOUND NAMES systemd)
if(SYSTEMD_FOUND)
  message(STATUS "Found at: " ${SYSTEMD_FOUND})
else(SYSTEMD_FOUND)
  message(FATAL_ERROR "Unable to find libsystemd. Configuration failed!")
endif(SYSTEMD_FOUND)

include(KDEInstallDirs)
include(KDECompilerSettings)
include(KDECMakeSettings)
include(FeatureSummary)
include(CheckIncludeFiles)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")

configure_file(config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/src/config.h)

add_definitions(-DTRANSLATION_DOMAIN=\"kcmsystemd\")

add_subdirectory(other)
add_subdirectory(src)

find_package(KF5I18n CONFIG REQUIRED)
ki18n_install(po)
