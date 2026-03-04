set(JYUTPING_SOURCES
    engine.cpp
)
add_fcitx5_addon(jyutping ${JYUTPING_SOURCES})
target_link_libraries(jyutping Fcitx5::Core Fcitx5::Config Fcitx5::Module::Spell Fcitx5::Module::Punctuation Fcitx5::Module::QuickPhrase LibIME::Jyutping)
target_compile_definitions(jyutping
    PRIVATE LIBIME_JYUTPING_INSTALL_PKGDATADIR=\"${LIBIME_JYUTPING_INSTALL_PKGDATADIR}\"
            FCITX_GETTEXT_DOMAIN=\"fcitx5-jyutping\"
)
fcitx5_add_i18n_definition(TARGETS jyutping)

install(TARGETS jyutping DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
fcitx5_translate_desktop_file(jyutping.conf.in jyutping.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/jyutping.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod" COMPONENT config)
configure_file(jyutping-addon.conf.in.in ${CMAKE_CURRENT_BINARY_DIR}/jyutping-addon.conf.in @ONLY)
fcitx5_translate_desktop_file(${CMAKE_CURRENT_BINARY_DIR}/jyutping-addon.conf.in jyutping-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/jyutping-addon.conf" RENAME jyutping.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon" COMPONENT config)


fcitx5_translate_desktop_file(org.fcitx.Fcitx5.Addon.Jyutping.metainfo.xml.in
                              org.fcitx.Fcitx5.Addon.Jyutping.metainfo.xml XML)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Jyutping.metainfo.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
