/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "libime/jyutping/jyutpingdata.h"
#include "libime/jyutping/jyutpingencoder.h"
#include <fcitx-utils/log.h>

using namespace libime;
using namespace libime::jyutping;

void dfs(const SegmentGraph &segs) {
    FCITX_ASSERT(segs.checkGraph());

    auto callback = [](const SegmentGraphBase &segs,
                       const std::vector<size_t> &path) {
        size_t s = 0;
        for (auto e : path) {
            std::cout << segs.segment(s, e) << " ";
            s = e;
        }
        std::cout << std::endl;
        return true;
    };

    segs.dfs(callback);
}

int main() {
    std::unordered_set<std::string> seen;
    for (auto &p : getJyutpingMap()) {
        auto jyutping = p.jyutping();
        auto initial = p.initial();
        auto final = p.final();

        if (p.fuzzy()) {
            continue;
        }

        auto fullJyutping = JyutpingEncoder::initialToString(initial) +
                            JyutpingEncoder::finalToString(final);
        // make sure valid item is unique
        auto result = seen.insert(jyutping);
        FCITX_ASSERT(result.second);

        int16_t encode =
            ((static_cast<int16_t>(initial) - JyutpingEncoder::firstInitial) *
             (JyutpingEncoder::lastFinal - JyutpingEncoder::firstFinal + 1)) +
            (static_cast<int16_t>(final) - JyutpingEncoder::firstFinal);
        FCITX_ASSERT(JyutpingEncoder::isValidInitialFinal(initial, final))
            << " " << encode;
        FCITX_ASSERT(fullJyutping == jyutping)
            << " " << fullJyutping << " " << jyutping;
        std::cout << encode << "," << std::endl;
    }

    dfs(JyutpingEncoder::parseUserJyutping("sangwut"));
    dfs(JyutpingEncoder::parseUserJyutping("ngng"));
    dfs(JyutpingEncoder::parseUserJyutping("ngaat"));
    dfs(JyutpingEncoder::parseUserJyutping("ngngaat"));
    dfs(JyutpingEncoder::parseUserJyutping("neizaudaajatgeoi"));
    dfs(JyutpingEncoder::parseUserJyutping("jinhauonjathaa"));
    dfs(JyutpingEncoder::parseUserJyutping("jinha"));
    dfs(JyutpingEncoder::parseUserJyutping("jinhau"));

    return 0;
}
