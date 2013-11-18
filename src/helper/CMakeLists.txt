# Set Include Directories
set(kcmsystemdhelper_INCLUDE_DIRS ${KDE4_INCLUDES})

# Set Sources
set(kcmsystemdhelper_SRCS helper.cpp)

# Set Link Libraries
set(kcmsystemdhelper_LINK_LIBS ${KDE4_KDECORE_LIBS})

# Build & Link
include_directories(${kcmsystemdhelper_INCLUDE_DIRS})
kde4_add_executable(kcmsystemdhelper ${kcmsystemdhelper_SRCS})
target_link_libraries(kcmsystemdhelper ${kcmsystemdhelper_LINK_LIBS})

# Install
install(TARGETS kcmsystemdhelper DESTINATION ${LIBEXEC_INSTALL_DIR})
kde4_install_auth_helper_files(kcmsystemdhelper org.kde.kcontrol.kcmsystemd root)
kde4_install_auth_actions(org.kde.kcontrol.kcmsystemd kcmsystemd.actions)
