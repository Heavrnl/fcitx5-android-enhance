
add_executable(libime_jyutpingdict libime_jyutpingdict.cpp)
target_link_libraries(libime_jyutpingdict LibIME::Jyutping)
install(TARGETS libime_jyutpingdict DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT tools)
add_executable(LibIME::jyutpingdict ALIAS libime_jyutpingdict)
