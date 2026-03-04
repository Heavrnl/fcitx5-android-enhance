/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "jyutpingcontext.h"
#include "jyutpingdecoder.h"
#include "jyutpingencoder.h"
#include "jyutpingime.h"
#include "jyutpingmatchstate.h"
#include "libime/core/historybigram.h"
#include "libime/core/userlanguagemodel.h"
#include <algorithm>
#include <fcitx-utils/log.h>
#include <iostream>

namespace libime {
namespace jyutping {

struct SelectedJyutping {
    SelectedJyutping(size_t s, WordNode word, std::string encodedJyutping)
        : offset_(s), word_(std::move(word)),
          encodedJyutping_(std::move(encodedJyutping)) {}
    size_t offset_;
    WordNode word_;
    std::string encodedJyutping_;
};

class JyutpingContextPrivate {
public:
    JyutpingContextPrivate(JyutpingContext *q, JyutpingIME *ime)
        : ime_(ime), matchState_(q) {}

    std::vector<std::vector<SelectedJyutping>> selected_;

    JyutpingIME *ime_;
    SegmentGraph segs_;
    Lattice lattice_;
    JyutpingMatchState matchState_;
    std::vector<SentenceResult> candidates_;
    std::vector<fcitx::ScopedConnection> conn_;
};

JyutpingContext::JyutpingContext(JyutpingIME *ime)
    : InputBuffer(fcitx::InputBufferOption::AsciiOnly),
      d_ptr(std::make_unique<JyutpingContextPrivate>(this, ime)) {
    FCITX_D();
    d->conn_.emplace_back(
        ime->connect<JyutpingIME::optionChanged>([this]() { clear(); }));
    d->conn_.emplace_back(
        ime->dict()->connect<JyutpingDictionary::dictionaryChanged>(
            [this](size_t) {
                FCITX_D();
                d->matchState_.clear();
            }));
}

JyutpingContext::~JyutpingContext() {}

bool JyutpingContext::typeImpl(const char *s, size_t length) {
    bool changed = cancelTill(cursor());
    changed = InputBuffer::typeImpl(s, length) || changed;
    if (changed) {
        update();
    }
    return changed;
}

void JyutpingContext::erase(size_t from, size_t to) {
    if (from == to) {
        return;
    }

    // check if erase everything
    if (from == 0 && to >= size()) {
        FCITX_D();
        d->candidates_.clear();
        d->selected_.clear();
        d->lattice_.clear();
        d->matchState_.clear();
        d->segs_ = SegmentGraph();
    } else {
        cancelTill(from);
    }
    InputBuffer::erase(from, to);

    if (size()) {
        update();
    }
}

void JyutpingContext::setCursor(size_t pos) {
    auto cancelled = cancelTill(pos);
    InputBuffer::setCursor(pos);
    if (cancelled) {
        update();
    }
}

int JyutpingContext::jyutpingBeforeCursor() const {
    FCITX_D();
    auto len = selectedLength();
    auto c = cursor();
    if (c < len) {
        return -1;
    }
    c -= len;
    if (d->candidates_.size()) {
        for (auto &s : d->candidates_[0].sentence()) {
            for (auto iter = s->path().begin(),
                      end = std::prev(s->path().end());
                 iter < end; iter++) {
                auto from = (*iter)->index(), to = (*std::next(iter))->index();
                if (to >= c) {
                    return from + len;
                }
            }
        }
    }
    return -1;
}

int JyutpingContext::jyutpingAfterCursor() const {
    FCITX_D();
    auto len = selectedLength();
    auto c = cursor();
    if (c < len) {
        return -1;
    }
    c -= len;
    if (d->candidates_.size()) {
        for (auto &s : d->candidates_[0].sentence()) {
            for (auto iter = s->path().begin(),
                      end = std::prev(s->path().end());
                 iter < end; iter++) {
                auto to = (*std::next(iter))->index();
                if (to > c) {
                    return to + len;
                }
            }
        }
    }
    return -1;
}

const std::vector<SentenceResult> &JyutpingContext::candidates() const {
    FCITX_D();
    return d->candidates_;
}

void JyutpingContext::select(size_t idx) {
    FCITX_D();
    assert(idx < d->candidates_.size());

    auto offset = selectedLength();

    d->selected_.emplace_back();

    auto &selection = d->selected_.back();
    for (auto &p : d->candidates_[idx].sentence()) {
        selection.emplace_back(
            offset + p->to()->index(),
            WordNode{p->word(), d->ime_->model()->index(p->word())},
            static_cast<const JyutpingLatticeNode *>(p)->encodedJyutping());
    }
    // add some special code for handling separator at the end
    auto remain = std::string_view(userInput()).substr(selectedLength());
    if (!remain.empty()) {
        if (std::all_of(remain.begin(), remain.end(),
                        [](char c) { return c == '\''; })) {
            selection.emplace_back(size(), WordNode("", 0), "");
        }
    }

    update();
}

bool JyutpingContext::cancelTill(size_t pos) {
    bool cancelled = false;
    while (selectedLength() > pos) {
        cancel();
        cancelled = true;
    }
    return cancelled;
}

void JyutpingContext::cancel() {
    FCITX_D();
    if (d->selected_.size()) {
        d->selected_.pop_back();
    }
    update();
}

State JyutpingContext::state() const {
    FCITX_D();
    auto model = d->ime_->model();
    State state = model->nullState();
    if (d->selected_.size()) {
        for (auto &s : d->selected_) {
            for (auto &item : s) {
                if (item.word_.word().empty()) {
                    continue;
                }
                State temp;
                model->score(state, item.word_, temp);
                state = std::move(temp);
            }
        }
    }
    return state;
}

void JyutpingContext::update() {
    FCITX_D();
    if (size() == 0) {
        clear();
        return;
    }

    if (selected()) {
        d->candidates_.clear();
    } else {
        size_t start = 0;
        auto model = d->ime_->model();
        State state = model->nullState();
        if (d->selected_.size()) {
            start = d->selected_.back().back().offset_;

            for (auto &s : d->selected_) {
                for (auto &item : s) {
                    if (item.word_.word().empty()) {
                        continue;
                    }
                    State temp;
                    model->score(state, item.word_, temp);
                    state = std::move(temp);
                }
            }
        }
        SegmentGraph newGraph = JyutpingEncoder::parseUserJyutping(
            userInput().substr(start), d->ime_->innerSegment());
        d->segs_.merge(
            newGraph,
            [d](const std::unordered_set<const SegmentGraphNode *> &nodes) {
                d->lattice_.discardNode(nodes);
                d->matchState_.discardNode(nodes);
            });
        assert(d->segs_.checkGraph());

        auto &graph = d->segs_;

        d->ime_->decoder()->decode(d->lattice_, d->segs_, d->ime_->nbest(),
                                   state, d->ime_->maxDistance(),
                                   d->ime_->minPath(), d->ime_->beamSize(),
                                   d->ime_->frameSize(), &d->matchState_);

        d->candidates_.clear();
        std::unordered_set<std::string> dup;
        for (size_t i = 0, e = d->lattice_.sentenceSize(); i < e; i++) {
            d->candidates_.push_back(d->lattice_.sentence(i));
            dup.insert(d->candidates_.back().toString());
        }

        auto bos = &graph.start();

        auto beginSize = d->candidates_.size();
        for (size_t i = graph.size(); i > 0; i--) {
            float min = 0;
            float max = -std::numeric_limits<float>::max();
            auto distancePenalty = d->ime_->model()->unknownPenalty() / 3;
            for (auto &graphNode : graph.nodes(i)) {
                auto distance = graph.distanceToEnd(graphNode);
                auto adjust = static_cast<float>(distance) * distancePenalty;
                for (auto &latticeNode : d->lattice_.nodes(&graphNode)) {
                    if (latticeNode.from() == bos) {
                        if (!d->ime_->model()->isNodeUnknown(latticeNode)) {
                            if (latticeNode.score() < min) {
                                min = latticeNode.score();
                            }
                            if (latticeNode.score() > max) {
                                max = latticeNode.score();
                            }
                        }
                        if (dup.count(latticeNode.word())) {
                            continue;
                        }
                        d->candidates_.push_back(
                            latticeNode.toSentenceResult(adjust));
                        dup.insert(latticeNode.word());
                    }
                }
            }
            for (auto &graphNode : graph.nodes(i)) {
                auto distance = graph.distanceToEnd(graphNode);
                auto adjust = static_cast<float>(distance) * distancePenalty;
                for (auto &latticeNode : d->lattice_.nodes(&graphNode)) {
                    if (latticeNode.from() != bos &&
                        latticeNode.score() > min &&
                        latticeNode.score() + d->ime_->maxDistance() > max) {
                        auto fullWord = latticeNode.fullWord();
                        if (dup.count(fullWord)) {
                            continue;
                        }
                        d->candidates_.push_back(
                            latticeNode.toSentenceResult(adjust));
                    }
                }
            }
        }
        std::sort(d->candidates_.begin() + beginSize, d->candidates_.end(),
                  std::greater<SentenceResult>());
    }

    if (cursor() < selectedLength()) {
        setCursor(selectedLength());
    }
}

bool JyutpingContext::selected() const {
    FCITX_D();
    if (userInput().empty()) {
        return false;
    }

    if (d->selected_.size()) {
        if (d->selected_.back().back().offset_ == size()) {
            return true;
        }
    }

    return false;
}

std::string JyutpingContext::selectedSentence() const {
    FCITX_D();
    std::string ss;
    for (auto &s : d->selected_) {
        for (auto &item : s) {
            ss += item.word_.word();
        }
    }
    return ss;
}

size_t JyutpingContext::selectedLength() const {
    FCITX_D();
    if (d->selected_.size()) {
        return d->selected_.back().back().offset_;
    }
    return 0;
}

std::string JyutpingContext::preedit() const {
    return preeditWithCursor().first;
}

std::pair<std::string, size_t> JyutpingContext::preeditWithCursor() const {
    FCITX_D();
    std::string ss = selectedSentence();
    auto len = selectedLength();
    auto c = cursor();
    size_t actualCursor = ss.size();
    // should not happen
    if (c < len) {
        c = len;
    }

    auto resultSize = ss.size();

    if (d->candidates_.size()) {
        bool first = true;
        for (auto &s : d->candidates_[0].sentence()) {
            for (auto iter = s->path().begin(),
                      end = std::prev(s->path().end());
                 iter < end; iter++) {
                if (!first) {
                    ss += " ";
                    resultSize += 1;
                } else {
                    first = false;
                }
                auto from = (*iter)->index(), to = (*std::next(iter))->index();
                if (c >= from + len && c < to + len) {
                    actualCursor = resultSize + c - from - len;
                }
                auto jyutping = d->segs_.segment(from, to);
                ss.append(jyutping.data(), jyutping.size());
                resultSize += jyutping.size();
            }
        }
    }
    if (c == size()) {
        actualCursor = resultSize;
    }
    return {ss, actualCursor};
}

std::vector<std::string> JyutpingContext::selectedWords() const {
    FCITX_D();
    std::vector<std::string> newSentence;
    for (auto &s : d->selected_) {
        for (auto &item : s) {
            if (!item.word_.word().empty()) {
                newSentence.push_back(item.word_.word());
            }
        }
    }
    return newSentence;
}

std::string JyutpingContext::selectedFullJyutping() const {
    FCITX_D();
    std::string jyutping;
    for (auto &s : d->selected_) {
        for (auto &item : s) {
            if (!item.word_.word().empty()) {
                if (!jyutping.empty()) {
                    jyutping.push_back('\'');
                }
                jyutping +=
                    JyutpingEncoder::decodeFullJyutping(item.encodedJyutping_);
            }
        }
    }
    return jyutping;
}

std::string JyutpingContext::candidateFullJyutping(size_t idx) const {
    FCITX_D();
    std::string jyutping;
    for (auto &p : d->candidates_[idx].sentence()) {
        if (!p->word().empty()) {
            if (!jyutping.empty()) {
                jyutping.push_back('\'');
            }
            jyutping += JyutpingEncoder::decodeFullJyutping(
                static_cast<const JyutpingLatticeNode *>(p)->encodedJyutping());
        }
    }
    return jyutping;
}

void JyutpingContext::learn() {
    FCITX_D();
    if (!selected()) {
        return;
    }

    if (learnWord()) {
        std::vector<std::string> newSentence{sentence()};
        d->ime_->model()->history().add(newSentence);
    } else {
        std::vector<std::string> newSentence;
        for (auto &s : d->selected_) {
            for (auto &item : s) {
                if (!item.word_.word().empty()) {
                    newSentence.push_back(item.word_.word());
                }
            }
        }
        d->ime_->model()->history().add(newSentence);
    }
}

bool JyutpingContext::learnWord() {
    FCITX_D();
    std::string ss;
    std::string jyutping;
    if (d->selected_.empty()) {
        return false;
    }
    // don't learn single character.
    if (d->selected_.size() == 1 && d->selected_[0].size() == 1) {
        return false;
    }
    for (auto &s : d->selected_) {
        bool first = true;
        for (auto &item : s) {
            if (!item.word_.word().empty()) {
                if (item.encodedJyutping_.size() != 2) {
                    return false;
                }
                if (first) {
                    first = false;
                    ss += item.word_.word();
                    if (!jyutping.empty()) {
                        jyutping.push_back('\'');
                    }
                    jyutping += JyutpingEncoder::decodeFullJyutping(
                        item.encodedJyutping_);
                } else {
                    return false;
                }
            }
        }
    }

    d->ime_->dict()->addWord(JyutpingDictionary::UserDict, jyutping, ss);

    return true;
}

JyutpingIME *JyutpingContext::ime() const {
    FCITX_D();
    return d->ime_;
}

} // namespace jyutping
} // namespace libime
