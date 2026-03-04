/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "libime/core/languagemodel.h"
#include "libime/jyutping/jyutpingdecoder.h"
#include "libime/jyutping/jyutpingdictionary.h"
#include "libime/jyutping/jyutpingencoder.h"
#include "testdir.h"
#include <fcitx-utils/log.h>
#include <limits>

using namespace libime;
using namespace libime::jyutping;

void testTime(JyutpingDictionary &, Decoder &decoder, const char *jyutping,
              int nbest = 1) {
    auto graph = JyutpingEncoder::parseUserJyutping(jyutping, true);
    Lattice lattice;
    decoder.decode(lattice, graph, nbest, decoder.model()->nullState(),
                   std::numeric_limits<float>::max(),
                   -std::numeric_limits<float>::max(), Decoder::beamSizeDefault,
                   Decoder::frameSizeDefault, nullptr);
    for (size_t i = 0, e = lattice.sentenceSize(); i < e; i++) {
        auto &sentence = lattice.sentence(i);
        for (auto &p : sentence.sentence()) {
            std::cout << p->word() << " ";
        }
        std::cout << sentence.score() << std::endl;
    }
}

int main() {
    JyutpingDictionary dict;
    dict.load(JyutpingDictionary::SystemDict,
              LIBIME_BINARY_DIR "/data/jyutping.dict",
              JyutpingDictFormat::Binary);
    LanguageModel model(LIBIME_BINARY_DIR "/data/zh_HK.lm");
    JyutpingDecoder decoder(&dict, &model);
    testTime(dict, decoder, "jinhau");

    return 0;
}
