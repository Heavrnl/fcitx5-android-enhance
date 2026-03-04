cmake_minimum_required(VERSION 3.12)
project(fcitx5-libthai VERSION 5.1.7)

set(REQUIRED_FCITX_VERSION 5.1.12)
find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMUninstallTarget)

find_package(PkgConfig REQUIRED)
find_package(Fcitx5Core ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Iconv REQUIRED)
find_package(Gettext REQUIRED)

if (NOT DEFINED THAI_TARGET)
    pkg_check_modules(LibThai IMPORTED_TARGET "libthai" REQUIRED)
    set(THAI_TARGET PkgConfig::LibThai)
endif()

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")
add_definitions(-DFCITX_GETTEXT_DOMAIN=\"fcitx5-libthai\")
fcitx5_add_i18n_definition()

add_subdirectory(src)
add_subdirectory(po)

fcitx5_translate_desktop_file(org.fcitx.Fcitx5.Addon.LibThai.metainfo.xml.in
                              org.fcitx.Fcitx5.Addon.LibThai.metainfo.xml XML)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.LibThai.metainfo.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
