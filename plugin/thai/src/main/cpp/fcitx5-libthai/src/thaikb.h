/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _FCITX5_LIBTHAI_THAIKB_H_
#define _FCITX5_LIBTHAI_THAIKB_H_

enum class ThaiKBMap { KETMANEE, PATTACHOTE, TIS820_2538 };

unsigned char ThaiKeycodeToChar(ThaiKBMap map, int keycode, int shiftLevel);

#endif // _FCITX5_LIBTHAI_THAIKB_H_
