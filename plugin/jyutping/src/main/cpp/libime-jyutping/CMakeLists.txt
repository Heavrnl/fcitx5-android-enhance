cmake_minimum_required(VERSION 3.7)
project(libime-jyutping VERSION 1.0.15)

set(REQUIRED_FCITX_VERSION 5.1.13)
find_package(ECM 1.0 REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

#########################################
# Options
#########################################
option(ENABLE_ENGINE "Enable input method for fcitx" On)
option(ENABLE_TEST "Build Test" On)
option(ENABLE_DOC "Build doxygen" Off)

#########################################
# Dependency
#########################################

include(GNUInstallDirs)
include(FeatureSummary)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(ECMSetupVersion)
include(ECMGenerateHeaders)
include(ECMGeneratePkgConfigFile)
include(ECMUninstallTarget)
find_package(PkgConfig REQUIRED)

pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET "libzstd")
find_package(Fcitx5Utils  ${REQUIRED_FCITX_VERSION} REQUIRED)

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

find_package(Boost 1.61 REQUIRED COMPONENTS iostreams)
set(LIBIME_JYUTPING_INSTALL_PKGDATADIR "${CMAKE_INSTALL_FULL_DATADIR}/libime")
set(LIBIME_JYUTPING_INSTALL_LIBDATADIR "${CMAKE_INSTALL_FULL_LIBDIR}/libime")

find_package(LibIMECore 1.0.16 REQUIRED)

if(ENABLE_ENGINE)
    find_package(Gettext REQUIRED)
    find_package(Fcitx5Core ${REQUIRED_FCITX_VERSION} REQUIRED)
    find_package(Fcitx5Module REQUIRED Spell Punctuation QuickPhrase)
    add_subdirectory(po)
endif()

if(ENABLE_TEST)
    enable_testing()
    add_subdirectory(test)
endif()

add_subdirectory(src)
add_subdirectory(tools)
add_subdirectory(data)

if (ENABLE_DOC)
  find_package(Doxygen REQUIRED)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.codedocs" FCITX_DOXYGEN_CONFIGURATION)
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
    ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    @ONLY)
  add_custom_target(doc
      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
