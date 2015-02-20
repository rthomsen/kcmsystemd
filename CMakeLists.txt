project(kcmsystemd)
set(KCM_SYSTEMD_VERSION_MAJOR "0")
set(KCM_SYSTEMD_VERSION_MINOR "7")
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
             ConfigWidgets
             CoreAddons
             I18n
             Auth)

include(KDEInstallDirs)
include(KDECompilerSettings)
include(KDECMakeSettings)
include(FeatureSummary)
include(CheckIncludeFiles)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")

configure_file(config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/src/config.h)

add_subdirectory(other)
add_subdirectory(src)
