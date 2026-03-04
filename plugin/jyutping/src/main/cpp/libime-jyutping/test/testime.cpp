/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "libime/core/historybigram.h"
#include "libime/core/lattice.h"
#include "libime/core/userlanguagemodel.h"
#include "libime/jyutping/jyutpingcontext.h"
#include "libime/jyutping/jyutpingdecoder.h"
#include "libime/jyutping/jyutpingdictionary.h"
#include "libime/jyutping/jyutpingime.h"
#include "testdir.h"
#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/null.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <fcitx-utils/log.h>
#include <fcitx-utils/stringutils.h>
#include <fstream>
#include <functional>
#include <sstream>

using namespace libime;
using namespace libime::jyutping;

int main(int argc, char *argv[]) {
    fcitx::Log::setLogRule("libime=5");
    JyutpingIME ime(std::make_unique<JyutpingDictionary>(),
                    std::make_unique<UserLanguageModel>(LIBIME_BINARY_DIR
                                                        "/data/zh_HK.lm"));
    ime.setNBest(2);
    ime.dict()->load(JyutpingDictionary::SystemDict,
                     LIBIME_BINARY_DIR "/data/jyutping.dict",
                     JyutpingDictFormat::Binary);
    if (argc >= 2) {
        ime.dict()->load(JyutpingDictionary::UserDict, argv[1],
                         JyutpingDictFormat::Binary);
    }
    if (argc >= 3) {
        std::fstream fin(argv[2], std::ios::in | std::ios::binary);
        ime.model()->history().load(fin);
    }
    ime.setScoreFilter(1.0f);
    JyutpingContext c(&ime);

    std::string word;
    while (std::cin >> word) {
        bool printAll = false;
        if (word == "back") {
            c.backspace();
        } else if (word == "reset") {
            c.clear();
        } else if (word.size() == 1 &&
                   (('a' <= word[0] && word[0] <= 'z') ||
                    (!c.userInput().empty() && word[0] == '\''))) {
            c.type(word);
        } else if (word.size() == 1 && ('0' <= word[0] && word[0] <= '9')) {
            size_t idx;
            if (word[0] == '0') {
                idx = 9;
            } else {
                idx = word[0] - '1';
            }
            if (c.candidates().size() >= idx) {
                c.select(idx);
            }
        } else if (word == "all") {
            printAll = true;
        }
        if (c.selected()) {
            std::cout << "COMMIT:   " << c.preedit() << std::endl;
            c.learn();
            c.clear();
            continue;
        }
        std::cout << "PREEDIT:  " << c.preedit() << std::endl;
        std::cout << "SENTENCE: " << c.sentence() << std::endl;
        size_t count = 1;
        for (auto &candidate : c.candidates()) {
            std::cout << (count % 10) << ": ";
            for (auto node : candidate.sentence()) {
                auto &jyutping = static_cast<const JyutpingLatticeNode *>(node)
                                     ->encodedJyutping();
                std::cout << node->word();
                if (!jyutping.empty()) {
                    std::cout << " "
                              << JyutpingEncoder::decodeFullJyutping(jyutping);
                }
            }
            std::cout << " " << candidate.score() << std::endl;
            count++;
            if (!printAll && count > 10) {
                break;
            }
        }
    }

    boost::iostreams::stream<boost::iostreams::null_sink> nullOstream(
        (boost::iostreams::null_sink()));
    ime.dict()->save(JyutpingDictionary::UserDict, nullOstream,
                     JyutpingDictFormat::Binary);
    ime.model()->history().dump(nullOstream);

    return 0;
}
