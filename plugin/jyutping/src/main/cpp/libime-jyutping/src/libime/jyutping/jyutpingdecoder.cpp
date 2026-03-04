/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "jyutpingdecoder_p.h"
#include <cmath>

namespace libime {
namespace jyutping {

JyutpingLatticeNode::JyutpingLatticeNode(
    std::string_view word, WordIndex idx, SegmentGraphPath path,
    const State &state, float cost,
    std::unique_ptr<JyutpingLatticeNodePrivate> data)
    : LatticeNode(word, idx, std::move(path), state, cost),
      d_ptr(std::move(data)) {}

JyutpingLatticeNode::~JyutpingLatticeNode() = default;

const std::string &JyutpingLatticeNode::encodedJyutping() const {
    static const std::string empty;
    if (!d_ptr) {
        return empty;
    }
    return d_ptr->encodedJyutping_;
}

LatticeNode *JyutpingDecoder::createLatticeNodeImpl(
    const SegmentGraphBase &graph, const LanguageModelBase *model,
    std::string_view word, WordIndex idx, SegmentGraphPath path,
    const State &state, float cost, std::unique_ptr<LatticeNodeData> data,
    bool onlyPath) const {
    std::unique_ptr<JyutpingLatticeNodePrivate> jyutpingData(
        static_cast<JyutpingLatticeNodePrivate *>(data.release()));
    if (model->isUnknown(idx, word)) {
        // we don't really care about a lot of unknown single character
        // which is not used for candidates
        if ((jyutpingData && jyutpingData->encodedJyutping_.size() == 2) &&
            path.front() != &graph.start() && !onlyPath) {
            return nullptr;
        }
    }

    return new JyutpingLatticeNode(word, idx, std::move(path), state, cost,
                                   std::move(jyutpingData));
}

} // namespace jyutping
} // namespace libime
