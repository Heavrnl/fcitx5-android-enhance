/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGDECODER_P_H_
#define _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGDECODER_P_H_

#include "jyutpingdecoder.h"
#include <libime/core/lattice.h>

namespace libime {
namespace jyutping {

class JyutpingLatticeNodePrivate : public LatticeNodeData {
public:
    JyutpingLatticeNodePrivate(std::string_view encodedJyutping)
        : encodedJyutping_(encodedJyutping) {}

    std::string encodedJyutping_;
};

} // namespace jyutping
} // namespace libime

#endif // _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGDECODER_P_H_
