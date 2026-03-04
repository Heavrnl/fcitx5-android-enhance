/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGMATCHSTATE_H_
#define _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGMATCHSTATE_H_

#include "libimejyutping_export.h"
#include <fcitx-utils/macros.h>
#include <libime/jyutping/jyutpingencoder.h>
#include <unordered_set>

namespace libime {

class SegmentGraphNode;

namespace jyutping {

class JyutpingMatchStatePrivate;
class JyutpingContext;

// Provides caching mechanism used by JyutpingContext.
class LIBIMEJYUTPING_EXPORT JyutpingMatchState {
    friend class JyutpingMatchContext;

public:
    JyutpingMatchState(JyutpingContext *context);
    ~JyutpingMatchState();

    // Invalidate everything in the state.
    void clear();

    // Invalidate a set of node, usually caused by the change of user input.
    void discardNode(const std::unordered_set<const SegmentGraphNode *> &node);

    // Invalidate a whole dictionary, usually caused by the change to the
    // dictionary.
    void discardDictionary(size_t idx);

private:
    std::unique_ptr<JyutpingMatchStatePrivate> d_ptr;
    FCITX_DECLARE_PRIVATE(JyutpingMatchState);
};

} // namespace jyutping
} // namespace libime

#endif // _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGMATCHSTATE_H_
