set(DICT_TAR "jyutping-dict-20180104.tar.xz")
set(DICT_URL "https://download.fcitx-im.org/data/${DICT_TAR}")

set(MODEL_TAR "jyutping-model-20180103.tar.xz")
set(MODEL_URL "https://download.fcitx-im.org/data/${MODEL_TAR}")

fcitx5_download(dict-download ${DICT_URL} ${DICT_TAR}
                e3a5b13edb8efa2f764245a3232f99ba7e7670e22b8cbe666a4fffa84b35f35b)
fcitx5_extract(dict-extract ${DICT_TAR} DEPENDS dict-download
                OUTPUT words.txt)

fcitx5_download(model-download ${MODEL_URL} ${MODEL_TAR}
                4f07229e2080f0ee30ce51b016409f260af82a58dd406a01ea5981b59ca87071)
fcitx5_extract(model-extract ${MODEL_TAR} DEPENDS model-download
                OUTPUT hk.arpa)

set(LM_SRC "${CMAKE_CURRENT_BINARY_DIR}/hk.arpa")
set(LM_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/zh_HK.lm")
set(LM_PREDICT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/zh_HK.lm.predict")

add_custom_command(
  OUTPUT "${LM_OUTPUT}"
  DEPENDS "${LM_SRC}" LibIME::slm_build_binary
  COMMAND LibIME::slm_build_binary -s -a 22 -q 8 trie "${LM_SRC}" "${LM_OUTPUT}")
add_custom_target(model ALL DEPENDS "${LM_OUTPUT}")

add_custom_command(
  OUTPUT "${LM_PREDICT_OUTPUT}"
  DEPENDS "${LM_SRC}" model LibIME::prediction
  COMMAND LibIME::prediction "${LM_OUTPUT}" "${LM_SRC}" "${LM_PREDICT_OUTPUT}")
add_custom_target(predict ALL DEPENDS "${LM_PREDICT_OUTPUT}")

set(DICT_SRC "${CMAKE_CURRENT_BINARY_DIR}/words.txt")
set(DICT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/jyutping.dict")
add_custom_command(
  OUTPUT "${DICT_OUTPUT}"
  DEPENDS "${DICT_SRC}" LibIME::jyutpingdict
  COMMAND LibIME::jyutpingdict "${DICT_SRC}" "${DICT_OUTPUT}")
add_custom_target(dict ALL DEPENDS "${DICT_OUTPUT}")
install(FILES "${DICT_OUTPUT}" DESTINATION "${LIBIME_JYUTPING_INSTALL_PKGDATADIR}")
install(FILES "${LM_OUTPUT}" "${LM_PREDICT_OUTPUT}" DESTINATION "${LIBIME_JYUTPING_INSTALL_LIBDATADIR}")
