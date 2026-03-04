/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _FCITX5_LIBTHAI_ENGINE_H_
#define _FCITX5_LIBTHAI_ENGINE_H_

#include "iconvwrapper.h"
#include "thaikb.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <thai/thinp.h>

namespace fcitx {

FCITX_CONFIG_ENUM_NAME_WITH_I18N(ThaiKBMap, N_("KETMANEE"), N_("PATTACHOTE"),
                                 N_("TIS820_2538"));
FCITX_CONFIG_ENUM_NAME_WITH_I18N(thstrict_t, N_("Passthrough"),
                                 N_("Basic check"), N_("Strict"));

FCITX_CONFIGURATION(
    LibThaiConfig,
    OptionWithAnnotation<ThaiKBMap, ThaiKBMapI18NAnnotation> keyboardMap{
        this, "KeyboardMap", _("Keyboard Map"), ThaiKBMap::KETMANEE};
    Option<bool> correction{this, "Correction", _("Correction"), true};
    OptionWithAnnotation<thstrict_t, thstrict_tI18NAnnotation> strictness{
        this, "Strictness", _("Strictness"), ISC_BASICCHECK};

);

class LibThaiState;

class LibThaiEngine final : public InputMethodEngine {
public:
    LibThaiEngine(Instance *instance);
    ~LibThaiEngine();

    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void deactivate(const fcitx::InputMethodEntry & /*entry*/,
                    fcitx::InputContextEvent &event) override;
    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &raw) override {
        config_.load(raw, true);
        safeSaveAsIni(config_, "conf/libthai.conf");
    }

    void reloadConfig() override { readAsIni(config_, "conf/libthai.conf"); }

    auto &convFromUtf8() const { return convFromUtf8_; }
    auto &convToUtf8() const { return convToUtf8_; }

private:
    Instance *instance_;
    IconvWrapper convFromUtf8_;
    IconvWrapper convToUtf8_;
    LibThaiConfig config_;
    FactoryFor<LibThaiState> factory_;
};

class LibThaiFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-libthai", FCITX_INSTALL_LOCALEDIR);
        return new LibThaiEngine(manager->instance());
    }
};
} // namespace fcitx

#endif // _FCITX5_LIBTHAI_ENGINE_H_
