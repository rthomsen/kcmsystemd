# Build & Link
add_executable(kcmsystemdhelper helper.cpp)
target_link_libraries(kcmsystemdhelper Qt5::DBus KF5::Auth)

# Install
install(TARGETS kcmsystemdhelper DESTINATION ${KAUTH_HELPER_INSTALL_DIR})
kauth_install_helper_files(kcmsystemdhelper org.kde.kcontrol.kcmsystemd root)
kauth_install_actions(org.kde.kcontrol.kcmsystemd kcmsystemd.actions)
