add_subdirectory(helper)

set(kcmsystemd_SRCS kcmsystemd.cpp
                    unitmodel.cpp
                    sortfilterunitmodel.cpp
                    confoption.cpp
                    confmodel.cpp
                    confdelegate.cpp)

find_package(Boost 1.45.0 COMPONENTS filesystem system chrono REQUIRED)

ki18n_wrap_ui(kcmsystemd_SRCS ../ui/kcmsystemd.ui)

add_library(kcm_systemd MODULE ${kcmsystemd_SRCS})
target_link_libraries(kcm_systemd
                      KF5::I18n
                      KF5::ConfigWidgets
                      KF5::Service
                      ${Boost_LIBRARIES}
                      -lsystemd)
install(TARGETS kcm_systemd DESTINATION ${CMAKE_INSTALL_PLUGINDIR})

qt5_use_modules(kcm_systemd Widgets DBus)
