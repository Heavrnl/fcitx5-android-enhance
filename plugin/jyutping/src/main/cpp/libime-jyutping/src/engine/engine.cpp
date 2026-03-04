/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "engine.h"
#include "libime/jyutping/jyutpingime.h"
#include "punctuation_public.h"
#include "spell_public.h"
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/fdstreambuf.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <istream>
#include <libime/core/historybigram.h>
#include <libime/core/languagemodel.h>
#include <libime/core/prediction.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/jyutping/jyutpingcontext.h>
#include <libime/jyutping/jyutpingdecoder.h>
#include <libime/jyutping/jyutpingdictionary.h>
#include <memory>
#include <ostream>
#include <quickphrase_public.h>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fcitx {

FCITX_DEFINE_LOG_CATEGORY(jyutping, "jyutping");

#define PINYIN_DEBUG() FCITX_LOGC(jyutping, Debug)

bool consumePreifx(std::string_view &view, std::string_view prefix) {
    if (boost::starts_with(view, prefix)) {
        view.remove_prefix(prefix.size());
        return true;
    }
    return false;
}

class JyutpingState : public InputContextProperty {
public:
    JyutpingState(JyutpingEngine *engine) : context_(engine->ime()) {}

    libime::jyutping::JyutpingContext context_;
    bool lastIsPunc_ = false;
    std::unique_ptr<EventSourceTime> cancelLastEvent_;

    std::vector<std::string> predictWords_;
};

class JyutpingPredictCandidateWord : public CandidateWord {
public:
    JyutpingPredictCandidateWord(JyutpingEngine *engine, std::string word)
        : CandidateWord(Text(word)), engine_(engine), word_(std::move(word)) {}

    void select(InputContext *inputContext) const override {
        inputContext->commitString(word_);
        auto state = inputContext->propertyFor(&engine_->factory());
        state->predictWords_.push_back(word_);
        // Max history size.
        constexpr size_t maxHistorySize = 5;
        if (state->predictWords_.size() > maxHistorySize) {
            state->predictWords_.erase(state->predictWords_.begin(),
                                       state->predictWords_.begin() +
                                           state->predictWords_.size() -
                                           maxHistorySize);
        }
        engine_->updatePredict(inputContext);
    }

    JyutpingEngine *engine_;
    std::string word_;
};

class StrokeCandidateWord : public CandidateWord {
public:
    StrokeCandidateWord(JyutpingEngine *engine, std::string hz,
                        const std::string &py)
        : engine_(engine), hz_(std::move(hz)) {
        if (py.empty()) {
            setText(Text(hz_));
        } else {
            setText(Text(_("{0} ({1})", hz_, py)));
        }
    }

    void select(InputContext *inputContext) const override {
        inputContext->commitString(hz_);
        engine_->doReset(inputContext);
    }

private:
    JyutpingEngine *engine_;
    std::string hz_;
};

class SpellCandidateWord : public CandidateWord {
public:
    SpellCandidateWord(JyutpingEngine *engine, std::string word)
        : engine_(engine), word_(std::move(word)) {
        setText(Text(word_));
    }

    void select(InputContext *inputContext) const override {
        auto *state = inputContext->propertyFor(&engine_->factory());
        auto &context = state->context_;
        inputContext->commitString(context.selectedSentence() + word_);
        engine_->doReset(inputContext);
    }

private:
    JyutpingEngine *engine_;
    std::string word_;
};

class JyutpingCandidateWord : public CandidateWord {
public:
    JyutpingCandidateWord(JyutpingEngine *engine, Text text, size_t idx)
        : CandidateWord(std::move(text)), engine_(engine), idx_(idx) {}

    void select(InputContext *inputContext) const override {
        auto *state = inputContext->propertyFor(&engine_->factory());
        auto &context = state->context_;
        if (idx_ >= context.candidates().size()) {
            return;
        }
        context.select(idx_);
        engine_->updateUI(inputContext);
    }

    JyutpingEngine *engine_;
    size_t idx_;
};

std::unique_ptr<CandidateList>
JyutpingEngine::predictCandidateList(const std::vector<std::string> &words) {
    if (words.empty()) {
        return nullptr;
    }
    auto candidateList = std::make_unique<CommonCandidateList>();
    for (const auto &word : words) {
        candidateList->append<JyutpingPredictCandidateWord>(this, word);
    }
    candidateList->setSelectionKey(selectionKeys_);
    candidateList->setPageSize(*config_.pageSize);
    if (candidateList->size()) {
        candidateList->setGlobalCursorIndex(0);
    }
    return candidateList;
}

void JyutpingEngine::initPredict(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto *state = inputContext->propertyFor(&factory_);
    auto &context = state->context_;
    auto lmState = context.state();
    state->predictWords_ = context.selectedWords();
    auto words = prediction_.predict(lmState, context.selectedWords(),
                                     *config_.predictionSize);
    if (auto candidateList = predictCandidateList(words)) {
        auto &inputPanel = inputContext->inputPanel();
        inputPanel.setCandidateList(std::move(candidateList));
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void JyutpingEngine::updatePredict(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto *state = inputContext->propertyFor(&factory_);
    auto words = prediction_.predict(state->predictWords_, *config_.pageSize);
    if (auto candidateList = predictCandidateList(words)) {
        auto &inputPanel = inputContext->inputPanel();
        inputPanel.setCandidateList(std::move(candidateList));
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

int englishNess(const std::string &input) {
    auto pys = stringutils::split(input, " ");
    constexpr int fullWeight = -2;
    constexpr int shortWeight = 3;
    constexpr int invalidWeight = 6;
    int weight = 0;
    for (auto iter = pys.begin(), end = pys.end(); iter != end; ++iter) {
        if (*iter == "ng") {
            weight += fullWeight;
        } else {
            auto firstChr = (*iter)[0];
            if (firstChr == '\'') {
                return 0;
            }
            if (firstChr == 'i' || firstChr == 'u' || firstChr == 'v') {
                weight += invalidWeight;
            } else if (iter->size() <= 2) {
                weight += shortWeight;
            } else if (iter->find_first_of("aeiou") != std::string::npos) {
                weight += fullWeight;
            } else {
                weight += shortWeight;
            }
        }
    }

    if (weight < 0) {
        return 0;
    }
    return (weight + 3) / 10;
}

bool isStroke(const std::string &input) {
    static const std::unordered_set<char> py{'h', 'p', 's', 'z', 'n'};
    return std::all_of(input.begin(), input.end(),
                       [](char c) { return py.count(c); });
}

void JyutpingEngine::updateUI(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto state = inputContext->propertyFor(&factory_);
    auto &context = state->context_;
    if (context.selected()) {
        auto sentence = context.sentence();
        if (!inputContext->capabilityFlags().testAny(
                CapabilityFlag::PasswordOrSensitive)) {
            context.learn();
        }
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        inputContext->commitString(sentence);
        if (*config_.predictionEnabled) {
            initPredict(inputContext);
        }
        context.clear();
        return;
    }

    if (context.userInput().size()) {
        const auto &candidates = context.candidates();
        auto &inputPanel = inputContext->inputPanel();
        if (context.candidates().size()) {
            auto candidateList = std::make_unique<CommonCandidateList>();
            size_t idx = 0;
            candidateList->setPageSize(*config_.pageSize);
            candidateList->setCursorPositionAfterPaging(
                CursorPositionAfterPaging::ResetToFirst);

            for (const auto &candidate : candidates) {
                auto candidateString = candidate.toString();
                candidateList->append<JyutpingCandidateWord>(
                    this, Text(std::move(candidateString)), idx);
                idx++;
            }
            int engNess;
            auto parsedPy =
                context.preedit().substr(context.selectedSentence().size());
            if (spell() && (engNess = englishNess(parsedPy))) {
                auto py = context.userInput().substr(context.selectedLength());
                auto results = spell()->call<ISpell::hintWithProvider>(
                    "en", SpellProvider::Custom, py, engNess);
                int idx = 1;
                for (auto &result : results) {
                    auto actualIdx = idx;
                    if (actualIdx > candidateList->totalSize()) {
                        actualIdx = candidateList->totalSize();
                    }

                    candidateList->insert(
                        actualIdx,
                        std::make_unique<SpellCandidateWord>(this, result));
                    idx++;
                }
            }

            candidateList->setSelectionKey(selectionKeys_);
            if (candidateList->size()) {
                candidateList->setGlobalCursorIndex(0);
            }
            inputPanel.setCandidateList(std::move(candidateList));
        }
        inputPanel.setClientPreedit(
            Text(context.sentence(), TextFormatFlag::Underline));
        auto preeditWithCursor = context.preeditWithCursor();
        Text preedit(preeditWithCursor.first);
        preedit.setCursor(preeditWithCursor.second);
        inputPanel.setPreedit(preedit);
#if 0
        {
            size_t count = 1;
            std::cout << "--------------------------" << std::endl;
            for (auto &candidate : context.candidates()) {
                std::cout << (count % 10) << ": ";
                for (auto node : candidate.sentence()) {
                    std::cout << node->word();
                    std::cout << " ";
                }
                std::cout << " " << candidate.score() << std::endl;
                count++;
                if (count > 20) {
                    break;
                }
            }
        }
#endif
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

JyutpingEngine::JyutpingEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &) { return new JyutpingState(this); }) {
    ime_ = std::make_unique<libime::jyutping::JyutpingIME>(
        std::make_unique<libime::jyutping::JyutpingDictionary>(),
        std::make_unique<libime::UserLanguageModel>(
            libime::DefaultLanguageModelResolver::instance()
                .languageModelFileForLanguage("zh_HK")));

    const auto &standardPath = StandardPaths::global();
    auto systemDictFile =
        standardPath.open(StandardPathsType::Data, "libime/jyutping.dict");
    if (systemDictFile.isValid()) {
        IFDStreamBuf buffer(systemDictFile.fd());
        std::istream in(&buffer);
        ime_->dict()->load(libime::jyutping::JyutpingDictionary::SystemDict, in,
                           libime::jyutping::JyutpingDictFormat::Binary);
    } else {
        ime_->dict()->load(libime::jyutping::JyutpingDictionary::SystemDict,
                           LIBIME_JYUTPING_INSTALL_PKGDATADIR "/jyutping.dict",
                           libime::jyutping::JyutpingDictFormat::Binary);
    }
    prediction_.setUserLanguageModel(ime_->model());

    do {
        auto file =
            standardPath.open(StandardPathsType::PkgData, "jyutping/user.dict",
                              StandardPathsMode::User);

        if (file.fd() < 0) {
            break;
        }

        try {
            IFDStreamBuf buffer(file.fd());
            std::istream in(&buffer);
            ime_->dict()->load(libime::jyutping::JyutpingDictionary::UserDict,
                               in,
                               libime::jyutping::JyutpingDictFormat::Binary);
        } catch (const std::exception &) {
        }
    } while (0);
    do {
        auto file =
            standardPath.open(StandardPathsType::PkgData,
                              "jyutping/user.history", StandardPathsMode::User);

        try {
            IFDStreamBuf buffer(file.fd());
            std::istream in(&buffer);
            ime_->model()->load(in);
        } catch (const std::exception &) {
        }
    } while (0);

    ime_->setScoreFilter(1);
    reloadConfig();
    instance_->inputContextManager().registerProperty("jyutpingState",
                                                      &factory_);
    KeySym syms[] = {
        FcitxKey_1, FcitxKey_2, FcitxKey_3, FcitxKey_4, FcitxKey_5,
        FcitxKey_6, FcitxKey_7, FcitxKey_8, FcitxKey_9, FcitxKey_0,
    };

    KeyStates states;
    for (auto sym : syms) {
        selectionKeys_.emplace_back(sym, states);
    }

    predictionAction_.setShortText(_("Prediction"));
    predictionAction_.setLongText(_("Show prediction words"));
    predictionAction_.setIcon(*config_.predictionEnabled
                                  ? "fcitx-remind-active"
                                  : "fcitx-remind-inactive");
    predictionAction_.connect<SimpleAction::Activated>(
        [this](InputContext *ic) {
            config_.predictionEnabled.setValue(!(*config_.predictionEnabled));
            predictionAction_.setIcon(*config_.predictionEnabled
                                          ? "fcitx-remind-active"
                                          : "fcitx-remind-inactive");
            predictionAction_.update(ic);
        });
    instance_->userInterfaceManager().registerAction("jyutping-prediction",
                                                     &predictionAction_);
}

JyutpingEngine::~JyutpingEngine() {}

void JyutpingEngine::reloadConfig() {
    readAsIni(config_, "conf/jyutping.conf");
    ime_->setNBest(*config_.nbest);
    ime_->setInnerSegment(*config_.inner);
}
void JyutpingEngine::activate(const fcitx::InputMethodEntry &,
                              fcitx::InputContextEvent &event) {
    auto *inputContext = event.inputContext();
    // Request full width.
    fullwidth();
    chttrans();
    for (auto actionName : {"chttrans", "punctuation", "fullwidth"}) {
        if (auto *action =
                instance_->userInterfaceManager().lookupAction(actionName)) {
            inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                                 action);
        }
    }
    inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                         &predictionAction_);
}

void JyutpingEngine::deactivate(const fcitx::InputMethodEntry &entry,
                                fcitx::InputContextEvent &event) {
    auto *inputContext = event.inputContext();
    if (event.type() == EventType::InputContextSwitchInputMethod) {
        auto *state = inputContext->propertyFor(&factory_);
        if (state->context_.size()) {
            inputContext->commitString(state->context_.userInput());
        }
    }
    reset(entry, event);
}

void JyutpingEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    FCITX_UNUSED(entry);
    PINYIN_DEBUG() << "Jyutping receive key: " << event.key() << " "
                   << event.isRelease();

    // by pass all key release
    if (event.isRelease()) {
        return;
    }

    // and by pass all modifier
    if (event.key().isModifier()) {
        return;
    }

    auto *inputContext = event.inputContext();
    auto *state = inputContext->propertyFor(&factory_);
    bool lastIsPunc = state->lastIsPunc_;
    state->lastIsPunc_ = false;
    // check if we can select candidate.
    auto candidateList = inputContext->inputPanel().candidateList();
    if (candidateList) {
        int idx = event.key().keyListIndex(selectionKeys_);
        if (idx >= 0) {
            event.filterAndAccept();
            if (idx < candidateList->size()) {
                candidateList->candidate(idx).select(inputContext);
            }
            return;
        }

        if (event.key().checkKeyList(*config_.prevPage)) {
            auto pageable = candidateList->toPageable();
            if (!pageable->hasPrev()) {
                if (pageable->usedNextBefore()) {
                    event.filterAndAccept();
                    return;
                }
            } else {
                event.filterAndAccept();
                pageable->prev();
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return;
            }
        }

        if (event.key().checkKeyList(*config_.nextPage)) {
            event.filterAndAccept();
            candidateList->toPageable()->next();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            return;
        }

        if (auto *movable = candidateList->toCursorMovable()) {
            if (event.key().checkKeyList(*config_.nextCandidate)) {
                movable->nextCandidate();
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                event.filterAndAccept();
                return;
            }
            if (event.key().checkKeyList(*config_.prevCandidate)) {
                movable->prevCandidate();
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                event.filterAndAccept();
                return;
            }
        }
    }

    // In prediction, as long as it's not candidate selection, clear, then
    // fallback
    // to remaining operation.
    if (!state->predictWords_.empty()) {
        state->predictWords_.clear();
        inputContext->inputPanel().reset();
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    if (event.key().isLAZ() ||
        (event.key().check(FcitxKey_apostrophe) && state->context_.size())) {
        // first v, use it to trigger quickphrase
        if (quickphrase() && event.key().check(FcitxKey_v) &&
            !state->context_.size()) {

            quickphrase()->call<IQuickPhrase::trigger>(
                inputContext, "", "v", "", "", Key(FcitxKey_None));
            event.filterAndAccept();
            return;
        }
        state->context_.type(Key::keySymToUTF8(event.key().sym()));
        event.filterAndAccept();
    } else if (state->context_.size()) {
        // key to handle when it is not empty.
        if (event.key().check(FcitxKey_BackSpace)) {
            if (state->context_.selectedLength()) {
                state->context_.cancel();
            } else {
                state->context_.backspace();
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Delete)) {
            state->context_.del();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Home)) {
            state->context_.setCursor(state->context_.selectedLength());
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_End)) {
            state->context_.setCursor(state->context_.size());
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Left)) {
            if (state->context_.cursor() == state->context_.selectedLength()) {
                state->context_.cancel();
            }
            auto cursor = state->context_.cursor();
            if (cursor > 0) {
                state->context_.setCursor(cursor - 1);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Right)) {
            auto cursor = state->context_.cursor();
            if (cursor < state->context_.size()) {
                state->context_.setCursor(cursor + 1);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Left, KeyState::Ctrl)) {
            if (state->context_.cursor() == state->context_.selectedLength()) {
                state->context_.cancel();
            }
            auto cursor = state->context_.jyutpingBeforeCursor();
            if (cursor >= 0) {
                state->context_.setCursor(cursor);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Right, KeyState::Ctrl)) {
            auto cursor = state->context_.jyutpingAfterCursor();
            if (cursor >= 0 &&
                static_cast<size_t>(cursor) <= state->context_.size()) {
                state->context_.setCursor(cursor);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Escape)) {
            state->context_.clear();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Return)) {
            inputContext->commitString(state->context_.userInput());
            state->context_.clear();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_space)) {
            auto candidateList = inputContext->inputPanel().candidateList();
            if (candidateList && candidateList->size()) {
                event.filterAndAccept();
                int idx = std::max(candidateList->cursorIndex(), 0);
                inputContext->inputPanel()
                    .candidateList()
                    ->candidate(idx)
                    .select(inputContext);
                return;
            }
        }
    } else {
        if (event.key().check(FcitxKey_BackSpace)) {
            if (lastIsPunc) {
                auto puncStr = punctuation()->call<IPunctuation::cancelLast>(
                    "zh_HK", inputContext);
                if (!puncStr.empty()) {
                    // forward the original key is the best choice.
                    auto ref = inputContext->watch();
                    state->cancelLastEvent_ =
                        instance()->eventLoop().addTimeEvent(
                            CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 300, 0,
                            [this, ref, puncStr](EventSourceTime *, uint64_t) {
                                if (auto *inputContext = ref.get()) {
                                    inputContext->commitString(puncStr);
                                    auto *state =
                                        inputContext->propertyFor(&factory_);
                                    state->cancelLastEvent_.reset();
                                }
                                return true;
                            });
                    event.filter();
                    return;
                }
            }
        }
    }
    if (!event.filtered()) {
        if (event.key().states().testAny(KeyState::SimpleMask)) {
            return;
        }
        // if it gonna commit something
        auto c = Key::keySymToUnicode(event.key().sym());
        if (c) {
            if (inputContext->inputPanel().candidateList() &&
                inputContext->inputPanel().candidateList()->size()) {
                inputContext->inputPanel().candidateList()->candidate(0).select(
                    inputContext);
            }
            auto punc = punctuation()->call<IPunctuation::pushPunctuation>(
                "zh_HK", inputContext, c);
            if (event.key().check(FcitxKey_semicolon) && quickphrase()) {
                auto keyString = utf8::UCS4ToUTF8(c);
                // s is punc or key
                auto output = punc.size() ? punc : keyString;
                // alt is key or empty
                auto altOutput = punc.size() ? keyString : "";
                // if no punc: key -> key (s = key, alt = empty)
                // if there's punc: key -> punc, return -> key (s = punc, alt =
                // key)
                std::string text;
                if (!output.empty()) {
                    if (!altOutput.empty()) {
                        text =

                            _("Press {0} for {1} and {2} for {3}", keyString,
                              output, _("Return"), altOutput);
                    } else {
                        text = _("Press {0} for {1}", keyString, altOutput);
                    }
                }
                quickphrase()->call<IQuickPhrase::trigger>(
                    inputContext, text, "", output, altOutput,
                    Key(FcitxKey_semicolon));
                event.filterAndAccept();
                return;
            }

            if (punc.size()) {
                event.filterAndAccept();
                inputContext->commitString(punc);
            }
            state->lastIsPunc_ = true;
        }
    }

    if (event.filtered() && event.accepted()) {
        updateUI(inputContext);
    }
}

void JyutpingEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto *inputContext = event.inputContext();
    doReset(inputContext);
}

void JyutpingEngine::doReset(InputContext *inputContext) {
    auto *state = inputContext->propertyFor(&factory_);
    state->context_.clear();
    state->predictWords_.clear();
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    // state->lastIsPunc_ = false;
}

void JyutpingEngine::save() {
    safeSaveAsIni(config_, "conf/jyutping.conf");
    const auto &standardPath = StandardPaths::global();
    standardPath.safeSave(
        StandardPathsType::PkgData, "jyutping/user.dict", [this](int fd) {
            OFDStreamBuf buffer(fd);
            std::ostream out(&buffer);
            try {
                ime_->dict()->save(
                    libime::jyutping::JyutpingDictionary::UserDict, out,
                    libime::jyutping::JyutpingDictFormat::Binary);
                return static_cast<bool>(out);
            } catch (const std::exception &) {
                return false;
            }
        });
    standardPath.safeSave(StandardPathsType::PkgData, "jyutping/user.history",
                          [this](int fd) {
                              OFDStreamBuf buffer(fd);
                              std::ostream out(&buffer);
                              try {
                                  ime_->model()->save(out);
                                  return true;
                              } catch (const std::exception &) {
                                  return false;
                              }
                          });
}
} // namespace fcitx

FCITX_ADDON_FACTORY_V2(jyutping, fcitx::JyutpingEngineFactory)
