cmake_minimum_required(VERSION 3.21)

project(anthy-unicode-cmake VERSION 0.0.0)

include(GNUInstallDirs)

set(CONF_DIR "${CMAKE_INSTALL_SYSCONFDIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
)

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

set(Anthy_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/anthy-unicode")

set(anthy_public_headers
    ${Anthy_ROOT}/anthy/anthy.h
    ${Anthy_ROOT}/anthy/dicutil.h
)

set(diclib_SRC
    ${Anthy_ROOT}/src-diclib/diclib.c
    ${Anthy_ROOT}/src-diclib/file_dic.c
    ${Anthy_ROOT}/src-diclib/filemap.c
    ${Anthy_ROOT}/src-diclib/xstr.c
    ${Anthy_ROOT}/src-diclib/xchar.c
    ${Anthy_ROOT}/src-diclib/alloc.c
    ${Anthy_ROOT}/src-diclib/conf.c
    ${Anthy_ROOT}/src-diclib/logger.c
    ${Anthy_ROOT}/src-diclib/ruleparser.c
)
# add_library(diclib "${diclib_SRC}")

set(worddic_SRC
    ${Anthy_ROOT}/src-worddic/word_dic.c 
    ${Anthy_ROOT}/src-worddic/dic_util.c
    ${Anthy_ROOT}/src-worddic/wtype.c
    ${Anthy_ROOT}/src-worddic/texttrie.c
    ${Anthy_ROOT}/src-worddic/textdict.c
    ${Anthy_ROOT}/src-worddic/record.c
    ${Anthy_ROOT}/src-worddic/word_lookup.c
    ${Anthy_ROOT}/src-worddic/use_dic.c
    ${Anthy_ROOT}/src-worddic/priv_dic.c
    ${Anthy_ROOT}/src-worddic/mem_dic.c
    ${Anthy_ROOT}/src-worddic/ext_ent.c
    ${Anthy_ROOT}/src-worddic/matrix.c
    ${Anthy_ROOT}/src-worddic/feature_set.c
)
add_library(anthydic-unicode "${diclib_SRC}" "${worddic_SRC}")
# S_IREAD S_IWRITE S_IEXEC are deprecated and does not exist on Android
# https://www.gnu.org/software/libc/manual/html_node/Permission-Bits.html
target_compile_definitions(anthydic-unicode PRIVATE
    S_IREAD=S_IRUSR
    S_IWRITE=S_IWUSR
)
target_compile_options(anthydic-unicode PRIVATE "-ffile-prefix-map=${Anthy_ROOT}=.")
target_include_directories(anthydic-unicode PUBLIC
    $<BUILD_INTERFACE:${Anthy_ROOT}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set(ordering_SRC
    ${Anthy_ROOT}/src-ordering/candswap.c
    ${Anthy_ROOT}/src-ordering/candsort.c
    ${Anthy_ROOT}/src-ordering/commit.c
    ${Anthy_ROOT}/src-ordering/relation.c
    ${Anthy_ROOT}/src-ordering/infosort.c
    ${Anthy_ROOT}/src-ordering/candhistory.c
)
# add_library(ordering "${ordering_SRC}")

set(splitter_SRC
    ${Anthy_ROOT}/src-splitter/wordlist.c
    ${Anthy_ROOT}/src-splitter/metaword.c
    ${Anthy_ROOT}/src-splitter/depgraph.c
    ${Anthy_ROOT}/src-splitter/splitter.c
    ${Anthy_ROOT}/src-splitter/evalborder.c
    ${Anthy_ROOT}/src-splitter/compose.c
    ${Anthy_ROOT}/src-splitter/lattice.c
    ${Anthy_ROOT}/src-splitter/segclass.c
)
# add_library(split "${splitter_SRC}")

set(main_SRC
    ${Anthy_ROOT}/src-main/main.c
    ${Anthy_ROOT}/src-main/context.c
)
add_library(anthy-unicode "${splitter_SRC}" "${ordering_SRC}" "${main_SRC}")
target_compile_definitions(anthy-unicode PRIVATE
    S_IREAD=S_IRUSR
    S_IWRITE=S_IWUSR
)
target_compile_options(anthy-unicode PRIVATE "-ffile-prefix-map=${Anthy_ROOT}=.")
target_include_directories(anthy-unicode PUBLIC
    $<BUILD_INTERFACE:${Anthy_ROOT}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(anthy-unicode
    m
    anthydic-unicode
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    AnthyConfigVersion.cmake
    VERSION "1.0.0"
    COMPATIBILITY SameMajorVersion
)

install(FILES ${anthy_public_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/anthy)
install(TARGETS anthy-unicode anthydic-unicode EXPORT AnthyTargets)
install(EXPORT AnthyTargets
        FILE AnthyTargets.cmake
        DESTINATION "lib/cmake"
        NAMESPACE Anthy::
)

configure_package_config_file(
    AnthyConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/AnthyConfig.cmake"
    INSTALL_DESTINATION "lib/cmake"
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AnthyConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/AnthyConfigVersion.cmake"
        DESTINATION "lib/cmake"
)

if (NOT ANDROID)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/anthy-unicode.pc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/anthy-unicode.pc"
        @ONLY
    )

    install(
      FILES
        "${CMAKE_CURRENT_BINARY_DIR}/anthy-unicode.pc"
      DESTINATION
        "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    )
endif()
