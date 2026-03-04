/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUPINGIME_H_
#define _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUPINGIME_H_

#include "libimejyutping_export.h"
#include <fcitx-utils/connectableobject.h>
#include <fcitx-utils/macros.h>
#include <libime/jyutping/jyutpingencoder.h>
#include <limits>
#include <memory>

namespace libime {

class UserLanguageModel;

namespace jyutping {

class JyutpingIMEPrivate;
class JyutpingDecoder;
class JyutpingDictionary;

/// \brief Provides shared data for JyutpingContext.
class LIBIMEJYUTPING_EXPORT JyutpingIME : public fcitx::ConnectableObject {
public:
    JyutpingIME(std::unique_ptr<JyutpingDictionary> dict,
                std::unique_ptr<UserLanguageModel> model);
    virtual ~JyutpingIME();

    bool innerSegment() const;
    void setInnerSegment(bool inner);
    size_t nbest() const;
    void setNBest(size_t n);
    size_t beamSize() const;
    void setBeamSize(size_t n);
    size_t frameSize() const;
    void setFrameSize(size_t n);
    void setScoreFilter(float maxDistance = std::numeric_limits<float>::max(),
                        float minPath = -std::numeric_limits<float>::max());

    float maxDistance() const;
    float minPath() const;

    JyutpingDictionary *dict();
    const JyutpingDictionary *dict() const;
    const JyutpingDecoder *decoder() const;
    UserLanguageModel *model();
    const UserLanguageModel *model() const;

    FCITX_DECLARE_SIGNAL(JyutpingIME, optionChanged, void());

private:
    std::unique_ptr<JyutpingIMEPrivate> d_ptr;
    FCITX_DECLARE_PRIVATE(JyutpingIME);
};

} // namespace jyutping
} // namespace libime

#endif // _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUPINGIME_H_
