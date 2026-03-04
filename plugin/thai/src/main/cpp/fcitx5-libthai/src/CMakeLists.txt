
add_library(iconvwrapper OBJECT iconvwrapper.cpp)
set_target_properties(iconvwrapper PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(iconvwrapper Fcitx5::Utils Iconv::Iconv)

set(LIBTHAI_SOURCES
    engine.cpp
    thaikb.cpp
)
add_fcitx5_addon(libthai ${LIBTHAI_SOURCES})
target_link_libraries(libthai iconvwrapper Fcitx5::Core ${THAI_TARGET} Iconv::Iconv)
target_include_directories(libthai PRIVATE ${PROJECT_BINARY_DIR})
set_target_properties(libthai PROPERTIES PREFIX "")
install(TARGETS libthai DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
fcitx5_translate_desktop_file(libthai.conf.in libthai.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libthai.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod" COMPONENT config)
configure_file(libthai-addon.conf.in.in libthai-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/libthai-addon.conf.in" libthai-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libthai-addon.conf" RENAME libthai.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon" COMPONENT config)

