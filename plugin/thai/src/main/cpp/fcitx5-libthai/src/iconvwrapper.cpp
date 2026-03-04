/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "iconvwrapper.h"
#include <cstddef>
#include <cstdint>
#include <fcitx-utils/utf8.h>
#include <iconv.h>
#include <iterator>
#include <memory>
#include <string_view>
#include <vector>

class IconvWrapperPrivate {
public:
    IconvWrapperPrivate(iconv_t conv) : conv_(conv) {}
    ~IconvWrapperPrivate() {
        if (conv_ != reinterpret_cast<iconv_t>(-1)) {
            iconv_close(conv_);
        }
    }
    iconv_t conv_;
};

IconvWrapper::IconvWrapper(const char *from, const char *to)
    : d_ptr(std::make_unique<IconvWrapperPrivate>(iconv_open(to, from))) {}

IconvWrapper::~IconvWrapper() {}

IconvWrapper::operator bool() const {
    return d_ptr->conv_ != reinterpret_cast<iconv_t>(-1);
}

std::vector<uint8_t> IconvWrapper::tryConvert(std::string_view s) const {
    iconv_t conv = d_ptr->conv_;
    for (auto iter = std::begin(s), e = std::end(s); iter != e;
         iter = fcitx::utf8::nextChar(iter)) {
        std::vector<uint8_t> result;
        result.resize(s.size() * 10);
        size_t byteLength = s.size();
        size_t byteRemains = result.size();
        char *data = const_cast<char *>(s.data());
        char *outData = reinterpret_cast<char *>(result.data());
        auto err = iconv(conv, &data, &byteLength, &outData, &byteRemains);
        if (err == static_cast<size_t>(-1)) {
            continue;
        }
        byteLength = 0;
        err = iconv(conv, nullptr, &byteLength, &outData, &byteRemains);
        if (err == static_cast<size_t>(-1)) {
            continue;
        }
        if (data != s.data() + s.size()) {
            continue;
        }
        result.resize(result.size() - byteRemains);
        return result;
    }
    return {};
}
