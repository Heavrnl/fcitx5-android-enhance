/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _LIBIME_JYUTPING_LIBIME_JYUTPING_UTILS_P_H_
#define _LIBIME_JYUTPING_LIBIME_JYUTPING_UTILS_P_H_

#include "endian_p.h"
#include <cstdint>
#include <iostream>

namespace libime {

template <typename T>
std::ostream &marshall(std::ostream &out, T data)
    requires(sizeof(T) == sizeof(uint32_t))
{
    union {
        uint32_t i;
        T v;
    } c;
    static_assert(sizeof(T) == sizeof(uint32_t),
                  "this function is only for 4 byte data");
    c.v = data;
    c.i = htobe32(c.i);
    return out.write(reinterpret_cast<char *>(&c.i), sizeof(c.i));
}

template <typename T>
std::istream &unmarshall(std::istream &in, T &data)
    requires(sizeof(T) == sizeof(uint32_t))
{
    union {
        uint32_t i;
        T v;
    } c;
    static_assert(sizeof(T) == sizeof(uint32_t),
                  "this function is only for 4 byte data");
    if (in.read(reinterpret_cast<char *>(&c.i), sizeof(c.i))) {
        c.i = be32toh(c.i);
        data = c.v;
    }
    return in;
}

template <typename E>
void throw_if_fail(bool fail, E &&e) {
    if (fail) {
        throw e;
    }
}

inline void throw_if_io_fail(const std::ios &s) {
    throw_if_fail(!s, std::ios_base::failure("io fail"));
}

} // namespace libime

#endif // _LIBIME_JYUTPING_LIBIME_JYUTPING_UTILS_P_H_
