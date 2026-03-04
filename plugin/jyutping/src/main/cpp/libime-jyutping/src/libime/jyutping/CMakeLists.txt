ecm_setup_version(PROJECT
                  VARIABLE_PREFIX IMEJyutping
                  PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/LibIMEJyutpingConfigVersion.cmake"
                  SOVERSION 1)

set(LIBIME_JYUTPING_HDRS
    jyutpingdictionary.h
    jyutpingencoder.h
    jyutpingdata.h
    jyutpingcontext.h
    jyutpingdecoder.h
    jyutpingime.h
    jyutpingmatchstate.h
    ${CMAKE_CURRENT_BINARY_DIR}/libimejyutping_export.h
    )

set(LIBIME_JYUTPING_SRCS
    jyutpingencoder.cpp
    jyutpingdata.cpp
    jyutpingdictionary.cpp
    jyutpingcontext.cpp
    jyutpingdecoder.cpp
    jyutpingime.cpp
    jyutpingmatchstate.cpp
    )

add_library(IMEJyutping SHARED ${LIBIME_JYUTPING_SRCS})
set_target_properties(IMEJyutping
                      PROPERTIES VERSION ${IMEJyutping_VERSION}
                      SOVERSION ${IMEJyutping_SOVERSION}
                      EXPORT_NAME Jyutping
)
target_include_directories(IMEJyutping PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_FULL_INCLUDEDIR}/LibIME>)

target_link_libraries(IMEJyutping PUBLIC Fcitx5::Utils Boost::boost LibIME::Core PRIVATE Boost::iostreams PkgConfig::ZSTD)

install(TARGETS IMEJyutping EXPORT LibIMEJyutpingTargets LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT lib)
install(FILES ${LIBIME_JYUTPING_HDRS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/LibIME/libime/jyutping" COMPONENT header)

add_library(LibIME::Jyutping ALIAS IMEJyutping)

configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/LibIMEJyutpingConfig.cmake.in"
                              "${CMAKE_CURRENT_BINARY_DIR}/LibIMEJyutpingConfig.cmake"
                              INSTALL_DESTINATION  "${CMAKE_INSTALL_LIBDIR}/cmake/LibIMEJyutping"
)

generate_export_header(IMEJyutping BASE_NAME LibIMEJyutping)

install(EXPORT LibIMEJyutpingTargets
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibIMEJyutping"
        FILE LibIMEJyutpingTargets.cmake
        NAMESPACE LibIME::
        COMPONENT Devel)

install(FILES  "${CMAKE_CURRENT_BINARY_DIR}/LibIMEJyutpingConfig.cmake"
               "${CMAKE_CURRENT_BINARY_DIR}/LibIMEJyutpingConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibIMEJyutping"
        COMPONENT Devel)

