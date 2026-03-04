/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _FCITX5_LIBTHAI_ICONWRAPPER_H_
#define _FCITX5_LIBTHAI_ICONWRAPPER_H_

#include <cstdint>
#include <fcitx-utils/macros.h>
#include <memory>
#include <string_view>
#include <vector>

class IconvWrapperPrivate;

class IconvWrapper {
public:
    IconvWrapper(const char *from, const char *to);

    ~IconvWrapper();

    operator bool() const;

    std::vector<uint8_t> tryConvert(std::string_view s) const;

private:
    FCITX_DECLARE_PRIVATE(IconvWrapper);
    std::unique_ptr<IconvWrapperPrivate> d_ptr;
};

#endif // _FCITX5_LIBTHAI_ICONWRAPPER_H_
