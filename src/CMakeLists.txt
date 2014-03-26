add_subdirectory(helper)

set(kcmsystemd_INCLUDE_DIRS ${KDE4_INCLUDES})

set(kcmsystemd_SRCS kcmsystemd.cpp confoption.cpp reslimits.cpp environ.cpp advanced.cpp capabilities.cpp)

find_package(Boost 1.45.0 COMPONENTS filesystem system chrono REQUIRED)

kde4_add_ui_files(kcmsystemd_SRCS ../ui/kcmsystemd.ui)
kde4_add_ui_files(kcmsystemd_SRCS ../ui/reslimits.ui)
kde4_add_ui_files(kcmsystemd_SRCS ../ui/environ.ui)
kde4_add_ui_files(kcmsystemd_SRCS ../ui/advanced.ui)
kde4_add_ui_files(kcmsystemd_SRCS ../ui/capabilities.ui)

include_directories(${kcmsystemd_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIR})
kde4_add_plugin(kcm_systemd ${kcmsystemd_SRCS})
target_link_libraries(kcm_systemd ${KDE4_KDEUI_LIBS} ${KDE4_KIO_LIBS} ${Boost_LIBRARIES})
install(TARGETS kcm_systemd DESTINATION ${PLUGIN_INSTALL_DIR})
