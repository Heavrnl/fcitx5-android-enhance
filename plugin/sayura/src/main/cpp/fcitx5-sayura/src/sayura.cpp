/*
 * SPDX-FileCopyrightText: 2012~2012 Yichao Yu
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "sayura.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace fcitx {

namespace {

struct SayuraConsonant {
    uint32_t character;
    uint32_t mahaprana;
    uint32_t sagngnaka;
    KeySym key;
};

constexpr std::array<SayuraConsonant, 40> consonants = {{
    {0xda4, 0x00, 0x00, FcitxKey_z},   {0xda5, 0x00, 0x00, FcitxKey_Z},
    {0xdc0, 0x00, 0x00, FcitxKey_w},   {0x200c, 0x00, 0x00, FcitxKey_W},
    {0xdbb, 0x00, 0x00, FcitxKey_r},   {0xdbb, 0x00, 0x00, FcitxKey_R},
    {0xdad, 0xdae, 0x00, FcitxKey_t},  {0xda7, 0xda8, 0x00, FcitxKey_T},
    {0xdba, 0x00, 0x00, FcitxKey_y},   {0xdba, 0x00, 0x00, FcitxKey_Y},
    {0xdb4, 0xdb5, 0x00, FcitxKey_p},  {0xdb5, 0xdb5, 0x00, FcitxKey_P},
    {0xdc3, 0xdc2, 0x00, FcitxKey_s},  {0xdc1, 0xdc2, 0x00, FcitxKey_S},
    {0xdaf, 0xdb0, 0xdb3, FcitxKey_d}, {0xda9, 0xdaa, 0xdac, FcitxKey_D},
    {0xdc6, 0x00, 0x00, FcitxKey_f},   {0xdc6, 0x00, 0x00, FcitxKey_F},
    {0xd9c, 0xd9d, 0xd9f, FcitxKey_g}, {0xd9f, 0xd9d, 0x00, FcitxKey_G},
    {0xdc4, 0xd83, 0x00, FcitxKey_h},  {0xdc4, 0x00, 0x00, FcitxKey_H},
    {0xda2, 0xda3, 0xda6, FcitxKey_j}, {0xda3, 0xda3, 0xda6, FcitxKey_J},
    {0xd9a, 0xd9b, 0x00, FcitxKey_k},  {0xd9b, 0xd9b, 0x00, FcitxKey_K},
    {0xdbd, 0x00, 0x00, FcitxKey_l},   {0xdc5, 0x00, 0x00, FcitxKey_L},
    {0xd82, 0x00, 0x00, FcitxKey_x},   {0xd9e, 0x00, 0x00, FcitxKey_X},
    {0xda0, 0xda1, 0x00, FcitxKey_c},  {0xda1, 0xda1, 0x00, FcitxKey_C},
    {0xdc0, 0x00, 0x00, FcitxKey_v},   {0xdc0, 0x00, 0x00, FcitxKey_V},
    {0xdb6, 0xdb7, 0xdb9, FcitxKey_b}, {0xdb7, 0xdb7, 0xdb9, FcitxKey_B},
    {0xdb1, 0x00, 0xd82, FcitxKey_n},  {0xdab, 0x00, 0xd9e, FcitxKey_N},
    {0xdb8, 0x00, 0x00, FcitxKey_m},   {0xdb9, 0x00, 0x00, FcitxKey_M},
}};

struct SayuraVowel {
    uint32_t single0;
    uint32_t double0;
    uint32_t single1;
    uint32_t double1;
    KeySym key;
};

const std::array<SayuraVowel, 12> vowels{{
    {0xd85, 0xd86, 0xdcf, 0xdcf, FcitxKey_a},
    {0xd87, 0xd88, 0xdd0, 0xdd1, FcitxKey_A},
    {0xd87, 0xd88, 0xdd0, 0xdd1, FcitxKey_q},
    {0xd91, 0xd92, 0xdd9, 0xdda, FcitxKey_e},
    {0xd91, 0xd92, 0xdd9, 0xdda, FcitxKey_E},
    {0xd89, 0xd8a, 0xdd2, 0xdd3, FcitxKey_i},
    {0xd93, 0x00, 0xddb, 0xddb, FcitxKey_I},
    {0xd94, 0xd95, 0xddc, 0xddd, FcitxKey_o},
    {0xd96, 0x00, 0xdde, 0xddf, FcitxKey_O},
    {0xd8b, 0xd8c, 0xdd4, 0xdd6, FcitxKey_u},
    {0xd8d, 0xd8e, 0xdd8, 0xdf2, FcitxKey_U},
    /* This one already exists in consonants[], not sure y it is also here.~ */
    {0xd8f, 0xd90, 0xd8f, 0xd90, FcitxKey_Z},
}};

template <typename T, size_t n>
std::unordered_map<KeySym, T> fillKeyMap(const std::array<T, n> &data) {
    std::unordered_map<KeySym, T> result;
    std::transform(
        data.begin(), data.end(), std::inserter(result, result.end()),
        [](const T &item) { return std::make_pair(item.key, item); });
    return result;
}

template <typename Map, typename Key>
const typename Map::mapped_type *findValue(const Map &map, const Key &key) {
    if (auto iter = map.find(key); iter != map.end()) {
        return &iter->second;
    }
    return nullptr;
}

template <size_t n>
std::unordered_map<uint32_t, SayuraConsonant>
fillConsonantMap(const std::array<SayuraConsonant, n> &data) {
    std::unordered_map<uint32_t, SayuraConsonant> result;
    for (const auto &item : data) {
        if (item.character && !result.count(item.character)) {
            result[item.character] = item;
        }
        if (item.mahaprana && !result.count(item.mahaprana)) {
            result[item.mahaprana] = item;
        }
        if (item.sagngnaka && !result.count(item.sagngnaka)) {
            result[item.sagngnaka] = item;
        }
    }
    return result;
}

const SayuraConsonant *findConsonant(uint32_t c) {
    static const auto map = fillConsonantMap(consonants);
    return findValue(map, c);
}

const SayuraConsonant *findConsonantByKey(KeySym sym) {
    static const auto map = fillKeyMap(consonants);
    return findValue(map, sym);
}

const SayuraVowel *findVowelByKey(KeySym sym) {
    static const auto map = fillKeyMap(vowels);
    return findValue(map, sym);
}

bool isConsonant(uint32_t c) { return (c >= 0x0d9a) && (c <= 0x0dc6); }

} // namespace

class SayuraState : public InputContextProperty {
public:
    SayuraState(InputContext &ic) : ic_(&ic) {}

    ~SayuraState() {}

    void commitPreedit() {
        std::string str = bufferToUTF8();
        ic_->commitString(str);
        buffer_.clear();
    }

    void reset() {
        buffer_.clear();
        updateUI();
    }

    void updateUI() {
        auto &inputPanel = ic_->inputPanel();
        inputPanel.reset();
        auto str = bufferToUTF8();
        if (!str.empty()) {
            Text preedit(str, TextFormatFlag::HighLight);
            preedit.setCursor(str.size());
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
                inputPanel.setClientPreedit(preedit);
            } else {
                inputPanel.setPreedit(preedit);
            }
        }
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    bool backspace() {
        if (buffer_.empty()) {
            return false;
        }
        buffer_.pop_back();
        return true;
    }

    void handleConsonant(const SayuraConsonant &consonant) {
        if (buffer_.empty()) {
            buffer_.push_back(consonant.character);
            return;
        }

        const auto *first = findConsonant(buffer_.front());
        if (first) {
            switch (consonant.key) {
            case FcitxKey_w:
                buffer_.push_back(0x0dca);
                return;
            case FcitxKey_W:
                buffer_.push_back(0x0dca);
                commitPreedit();
                buffer_.push_back(0x200d);
                return;
            case FcitxKey_H:
                if (!first->mahaprana) {
                    break;
                }
                if (buffer_.empty()) {
                    return;
                }
                buffer_.pop_back();
                buffer_.push_back(first->mahaprana);
                return;
            case FcitxKey_G:
                if (!first->sagngnaka) {
                    break;
                }
                if (buffer_.empty()) {
                    return;
                }
                buffer_.pop_back();
                buffer_.push_back(first->sagngnaka);
                return;
            case FcitxKey_R:
                buffer_.push_back(0x0dca);
                buffer_.push_back(0x200d);
                commitPreedit();
                buffer_.push_back(0x0dbb);
                return;
            case FcitxKey_Y:
                buffer_.push_back(0x0dca);
                buffer_.push_back(0x200d);
                commitPreedit();
                buffer_.push_back(0x0dba);
                return;
            default:
                break;
            }
        }
        commitPreedit();
        buffer_.push_back(consonant.character);
    }

    void handleVowel(const SayuraVowel &vowel) {
        if (buffer_.empty()) {
            buffer_.push_back(vowel.single0);
            return;
        }
        auto c1 = buffer_.back();
        if (isConsonant(c1)) {
            buffer_.push_back(vowel.single1);
        } else if (c1 == vowel.single0) {
            buffer_.pop_back();
            buffer_.push_back(vowel.double0);
        } else if (c1 == vowel.single1) {
            buffer_.pop_back();
            buffer_.push_back(vowel.double1);
        } else if ((c1 == 0x0d86 || c1 == 0x0d87) && vowel.key == FcitxKey_a) {
            buffer_.pop_back();
            buffer_.push_back(vowel.single0 + 1);
        }
    }

private:
    std::string bufferToUTF8() {
        std::string str;
        for (auto c : buffer_) {
            str.append(utf8::UCS4ToUTF8(c));
        }
        return str;
    }

    InputContext *ic_;
    std::vector<uint32_t> buffer_;
};

SayuraEngine::SayuraEngine(Instance *instance)
    : instance_(instance),
      factory_([](InputContext &ic) { return new SayuraState(ic); }) {
    instance->inputContextManager().registerProperty("sayuraState", &factory_);
}

SayuraEngine::~SayuraEngine() {}

void SayuraEngine::activate(const InputMethodEntry & /*entry*/,
                            InputContextEvent & /*event*/) {}

void SayuraEngine::deactivate(const InputMethodEntry & /*entry*/,
                              InputContextEvent &event) {
    auto *state = event.inputContext()->propertyFor(&factory_);
    state->commitPreedit();
    state->updateUI();
}

void SayuraEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) {
    auto key = keyEvent.key();
    auto *ic = keyEvent.inputContext();
    auto *state = ic->propertyFor(&factory_);
    if (keyEvent.isRelease()) {
        return;
    }
    if (key.check(FcitxKey_Escape)) {
        reset(entry, keyEvent);
        return;
    }
    if (key.check(FcitxKey_BackSpace)) {
        if (state->backspace()) {
            state->updateUI();
            keyEvent.filterAndAccept();
        }
        return;
    }

    if (key.check(FcitxKey_space)) {
        state->commitPreedit();
        state->updateUI();
        return;
    }

    if (key.states() != KeyState::NoState) {
        return;
    }
    const auto *consonant = findConsonantByKey(key.sym());
    if (consonant) {
        state->handleConsonant(*consonant);
        state->updateUI();
        keyEvent.filterAndAccept();
        return;
    }

    const auto *vowel = findVowelByKey(key.sym());
    if (vowel) {
        state->handleVowel(*vowel);
        state->updateUI();
        keyEvent.filterAndAccept();
        return;
    }

    if (key.sym() == FcitxKey_Shift_L || key.sym() == FcitxKey_Shift_R) {
        return;
    }

    state->commitPreedit();
    state->updateUI();
}

void SayuraEngine::reset(const InputMethodEntry & /*entry*/,
                         InputContextEvent &event) {
    auto *state = event.inputContext()->propertyFor(&factory_);
    state->reset();
}

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(sayura, fcitx::SayuraFactory);
