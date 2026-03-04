/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _LIBIME_JYUTPING_ENGINE_ENGINE_H_
#define _LIBIME_JYUTPING_ENGINE_ENGINE_H_

#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <libime/core/prediction.h>
#include <libime/jyutping/jyutpingime.h>
#include <memory>

namespace fcitx {

FCITX_CONFIGURATION(
    JyutpingEngineConfig,
    Option<int, IntConstrain> pageSize{this, "PageSize", "Page size", 5,
                                       IntConstrain(3, 10)};
    Option<int, IntConstrain> predictionSize{
        this, "PredictionSize", "Prediction Size", 10, IntConstrain(3, 20)};
    Option<bool> predictionEnabled{this, "Prediction", "Enable Prediction ",
                                   false};
    KeyListOption prevPage{
        this,
        "PrevPage",
        "Prev Page",
        {Key(FcitxKey_minus), Key(FcitxKey_Up)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption nextPage{
        this,
        "NextPage",
        "Next Page",
        {Key(FcitxKey_equal), Key(FcitxKey_Down)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption prevCandidate{
        this,
        "PrevCandidate",
        "Prev Candidate",
        {Key("Shift+Tab")},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption nextCandidate{
        this,
        "NextCandidate",
        "Next Candidate",
        {Key("Tab")},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    Option<int, IntConstrain> nbest{this, "Number of sentence",
                                    "Number of Sentence", 2,
                                    IntConstrain(1, 3)};
    Option<bool> inner{this, "InnerSegment",
                       "Use Inner Segment Jyutping Settings", true};);

class JyutpingState;
class EventSourceTime;
class CandidateList;

class JyutpingEngine final : public InputMethodEngine {
public:
    JyutpingEngine(Instance *instance);
    ~JyutpingEngine();
    Instance *instance() { return instance_; }
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void reloadConfig() override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void doReset(InputContext *ic);
    void save() override;
    auto &factory() { return factory_; }

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        safeSaveAsIni(config_, "conf/jyutping.conf");
        reloadConfig();
    }

    libime::jyutping::JyutpingIME *ime() { return ime_.get(); }

    void initPredict(InputContext *ic);
    void updatePredict(InputContext *ic);
    std::unique_ptr<CandidateList>
    predictCandidateList(const std::vector<std::string> &words);
    void updateUI(InputContext *inputContext);

private:
    Instance *instance_;
    JyutpingEngineConfig config_;
    std::unique_ptr<libime::jyutping::JyutpingIME> ime_;
    KeyList selectionKeys_;
    FactoryFor<JyutpingState> factory_;
    SimpleAction predictionAction_;
    libime::Prediction prediction_;

    FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(spell, instance_->addonManager());
};

class JyutpingEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-jyutping", FCITX_INSTALL_LOCALEDIR);
        return new JyutpingEngine(manager->instance());
    }
};
} // namespace fcitx

#endif // _LIBIME_JYUTPING_ENGINE_ENGINE_H_
