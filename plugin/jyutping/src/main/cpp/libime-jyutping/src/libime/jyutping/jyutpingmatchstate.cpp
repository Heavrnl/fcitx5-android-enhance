/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "jyutpingcontext.h"
#include "jyutpingime.h"
#include "jyutpingmatchstate_p.h"

namespace libime {
namespace jyutping {

JyutpingMatchState::JyutpingMatchState(JyutpingContext *context)
    : d_ptr(std::make_unique<JyutpingMatchStatePrivate>(context)) {}
JyutpingMatchState::~JyutpingMatchState() {}

void JyutpingMatchState::clear() {
    FCITX_D();
    d->matchedPaths_.clear();
    d->nodeCacheMap_.clear();
    d->matchCacheMap_.clear();
}

void JyutpingMatchState::discardNode(
    const std::unordered_set<const SegmentGraphNode *> &nodes) {
    FCITX_D();
    for (auto node : nodes) {
        d->matchedPaths_.erase(node);
    }
    for (auto &p : d->matchedPaths_) {
        auto &l = p.second;
        auto iter = l.begin();
        while (iter != l.end()) {
            if (nodes.count(iter->path_.front())) {
                iter = l.erase(iter);
            } else {
                iter++;
            }
        }
    }
}

void JyutpingMatchState::discardDictionary(size_t idx) {
    FCITX_D();
    d->matchCacheMap_.erase(d->context_->ime()->dict()->trie(idx));
    d->nodeCacheMap_.erase(d->context_->ime()->dict()->trie(idx));
}

} // namespace jyutping
} // namespace libime
