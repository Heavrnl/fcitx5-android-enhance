/*
 * SPDX-FileCopyrightText: 2012~2012 Yichao Yu
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _FCITX5_SAYURA_SAYURA_H_
#define _FCITX5_SAYURA_SAYURA_H_

#include <fcitx-utils/i18n.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

namespace fcitx {

class SayuraState;

class SayuraEngine final : public InputMethodEngine {
public:
    SayuraEngine(Instance *instance);
    ~SayuraEngine();

    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void deactivate(const fcitx::InputMethodEntry & /*entry*/,
                    fcitx::InputContextEvent &event) override;

private:
    Instance *instance_;
    FactoryFor<SayuraState> factory_;
};

class SayuraFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-sayura", FCITX_INSTALL_LOCALEDIR);
        return new SayuraEngine(manager->instance());
    }
};

} // namespace fcitx

#endif // _FCITX5_SAYURA_SAYURA_H_
