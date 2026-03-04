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

#include "libime/jyutping/jyutpingdictionary.h"
#include "libime/jyutping/jyutpingencoder.h"
#include "testdir.h"
#include <fcitx-utils/log.h>

int main() {
    using namespace libime;
    using namespace libime::jyutping;
    JyutpingDictionary dict;
    dict.load(JyutpingDictionary::SystemDict,
              LIBIME_BINARY_DIR "/data/jyutping.dict",
              JyutpingDictFormat::Binary);

    char c[] = {static_cast<char>(JyutpingInitial::J),
                static_cast<char>(JyutpingFinal::IN),
                static_cast<char>(JyutpingInitial::H),
                static_cast<char>(JyutpingFinal::AU)};
    dict.matchWords(c, 2,
                    [c](std::string_view encodedJyutping,
                        std::string_view hanzi, float cost) {
                        std::cout << JyutpingEncoder::decodeFullJyutping(
                                         encodedJyutping)
                                  << " " << hanzi << " " << cost << std::endl;
                        return true;
                    });
    dict.matchWords(c + 2, 2,
                    [c](std::string_view encodedJyutping,
                        std::string_view hanzi, float cost) {
                        std::cout << JyutpingEncoder::decodeFullJyutping(
                                         encodedJyutping)
                                  << " " << hanzi << " " << cost << std::endl;
                        return true;
                    });

    auto graph = JyutpingEncoder::parseUserJyutping("jinhau", true);
    dict.matchPrefix(graph,
                     [&graph](const SegmentGraphPath &path, WordNode &node,
                              float, std::unique_ptr<LatticeNodeData>) {
                         for (auto &step : path) {
                             std::cout << step->index() << " ";
                         }
                         std::cout << node.word() << std::endl;
                         return true;
                     });

    dict.save(0, LIBIME_BINARY_DIR "/test/testjyutpingdictionary.dict",
              JyutpingDictFormat::Binary);
    // dict.save(0, std::cout, JyutpingDictFormat::Text);
    return 0;
}
