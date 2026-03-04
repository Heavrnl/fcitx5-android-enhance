/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "jyutpingime.h"
#include "jyutpingdecoder.h"
#include "libime/core/userlanguagemodel.h"

namespace libime {
namespace jyutping {

class JyutpingIMEPrivate : fcitx::QPtrHolder<JyutpingIME> {
public:
    JyutpingIMEPrivate(JyutpingIME *q, std::unique_ptr<JyutpingDictionary> dict,
                       std::unique_ptr<UserLanguageModel> model)
        : fcitx::QPtrHolder<JyutpingIME>(q), dict_(std::move(dict)),
          model_(std::move(model)), decoder_(std::make_unique<JyutpingDecoder>(
                                        dict_.get(), model_.get())) {}

    FCITX_DEFINE_SIGNAL_PRIVATE(JyutpingIME, optionChanged);

    std::unique_ptr<JyutpingDictionary> dict_;
    std::unique_ptr<UserLanguageModel> model_;
    std::unique_ptr<JyutpingDecoder> decoder_;
    bool innerSegment_ = true;
    size_t nbest_ = 1;
    size_t beamSize_ = Decoder::beamSizeDefault;
    size_t frameSize_ = Decoder::frameSizeDefault;
    float maxDistance_ = std::numeric_limits<float>::max();
    float minPath_ = -std::numeric_limits<float>::max();
};

JyutpingIME::JyutpingIME(std::unique_ptr<JyutpingDictionary> dict,
                         std::unique_ptr<UserLanguageModel> model)
    : d_ptr(std::make_unique<JyutpingIMEPrivate>(this, std::move(dict),
                                                 std::move(model))) {}

JyutpingIME::~JyutpingIME() {}

bool JyutpingIME::innerSegment() const {
    FCITX_D();
    return d->innerSegment_;
}

void JyutpingIME::setInnerSegment(bool inner) {
    FCITX_D();
    d->innerSegment_ = inner;
    emit<JyutpingIME::optionChanged>();
}

JyutpingDictionary *JyutpingIME::dict() {
    FCITX_D();
    return d->dict_.get();
}

const JyutpingDictionary *JyutpingIME::dict() const {
    FCITX_D();
    return d->dict_.get();
}

const JyutpingDecoder *JyutpingIME::decoder() const {
    FCITX_D();
    return d->decoder_.get();
}

UserLanguageModel *JyutpingIME::model() {
    FCITX_D();
    return d->model_.get();
}

const UserLanguageModel *JyutpingIME::model() const {
    FCITX_D();
    return d->model_.get();
}

size_t JyutpingIME::nbest() const {
    FCITX_D();
    return d->nbest_;
}

void JyutpingIME::setNBest(size_t n) {
    FCITX_D();
    if (d->nbest_ != n) {
        d->nbest_ = n;
        emit<JyutpingIME::optionChanged>();
    }
}

size_t JyutpingIME::beamSize() const {
    FCITX_D();
    return d->beamSize_;
}

void JyutpingIME::setBeamSize(size_t n) {
    FCITX_D();
    if (d->beamSize_ != n) {
        d->beamSize_ = n;
        emit<JyutpingIME::optionChanged>();
    }
}

size_t JyutpingIME::frameSize() const {
    FCITX_D();
    return d->frameSize_;
}

void JyutpingIME::setFrameSize(size_t n) {
    FCITX_D();
    if (d->frameSize_ != n) {
        d->frameSize_ = n;
        emit<JyutpingIME::optionChanged>();
    }
}

void JyutpingIME::setScoreFilter(float maxDistance, float minPath) {
    FCITX_D();
    if (d->maxDistance_ != maxDistance || d->minPath_ != minPath) {
        d->maxDistance_ = maxDistance;
        d->minPath_ = minPath;
        emit<JyutpingIME::optionChanged>();
    }
}

float JyutpingIME::maxDistance() const {
    FCITX_D();
    return d->maxDistance_;
}

float JyutpingIME::minPath() const {
    FCITX_D();
    return d->minPath_;
}
} // namespace jyutping
} // namespace libime
