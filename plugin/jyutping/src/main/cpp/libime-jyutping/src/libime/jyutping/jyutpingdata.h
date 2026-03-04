/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_LIBIME_JYUTPING_JYUTPINGDATA_H_
#define _FCITX_LIBIME_JYUTPING_JYUTPINGDATA_H_

#include "libimejyutping_export.h"
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index_container.hpp>
#include <libime/jyutping/jyutpingencoder.h>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace libime {
namespace jyutping {
struct JyutpingHash {
    std::size_t operator()(std::string_view const &val) const {
        return boost::hash_range(val.begin(), val.end());
    }
};

class JyutpingEntry {
public:
    JyutpingEntry(const char *jyutping, JyutpingInitial initial,
                  JyutpingFinal final, bool fuzzy = false)
        : jyutping_(jyutping), initial_(initial), final_(final), fuzzy_(fuzzy) {
    }

    std::string_view jyutpingView() const { return jyutping_; }
    const std::string &jyutping() const { return jyutping_; }
    JyutpingInitial initial() const { return initial_; }
    JyutpingFinal final() const { return final_; }
    bool fuzzy() const { return fuzzy_; }

private:
    std::string jyutping_;
    JyutpingInitial initial_;
    JyutpingFinal final_;
    bool fuzzy_;
};

using JyutpingMap = boost::multi_index_container<
    JyutpingEntry,
    boost::multi_index::indexed_by<boost::multi_index::hashed_non_unique<
        boost::multi_index::const_mem_fun<JyutpingEntry, std::string_view,
                                          &JyutpingEntry::jyutpingView>,
        JyutpingHash>>>;

LIBIMEJYUTPING_EXPORT
const JyutpingMap &getJyutpingMap();
LIBIMEJYUTPING_EXPORT const std::vector<bool> &getEncodedInitialFinal();

LIBIMEJYUTPING_EXPORT const
    std::unordered_map<std::string, std::pair<std::string, std::string>> &
    getInnerSegment();

} // namespace jyutping
} // namespace libime

#endif // _FCITX_LIBIME_JYUTPING_JYUTPINGDATA_H_
