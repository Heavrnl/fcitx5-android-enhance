/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGDECODER_H_
#define _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGDECODER_H_

#include "libimejyutping_export.h"
#include <libime/core/decoder.h>
#include <libime/jyutping/jyutpingdictionary.h>

namespace libime {
namespace jyutping {

class JyutpingLatticeNodePrivate;

class LIBIMEJYUTPING_EXPORT JyutpingLatticeNode : public LatticeNode {
public:
    JyutpingLatticeNode(std::string_view word, WordIndex idx,
                        SegmentGraphPath path, const State &state, float cost,
                        std::unique_ptr<JyutpingLatticeNodePrivate> data);
    virtual ~JyutpingLatticeNode();

    const std::string &encodedJyutping() const;

private:
    std::unique_ptr<JyutpingLatticeNodePrivate> d_ptr;
};

class LIBIMEJYUTPING_EXPORT JyutpingDecoder : public Decoder {
public:
    JyutpingDecoder(const JyutpingDictionary *dict,
                    const LanguageModelBase *model)
        : Decoder(dict, model) {}

protected:
    LatticeNode *createLatticeNodeImpl(const SegmentGraphBase &graph,
                                       const LanguageModelBase *model,
                                       std::string_view word, WordIndex idx,
                                       SegmentGraphPath path,
                                       const State &state, float cost,
                                       std::unique_ptr<LatticeNodeData> data,
                                       bool onlyPath) const override;
};
} // namespace jyutping
} // namespace libime

#endif // _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGDECODER_H_
