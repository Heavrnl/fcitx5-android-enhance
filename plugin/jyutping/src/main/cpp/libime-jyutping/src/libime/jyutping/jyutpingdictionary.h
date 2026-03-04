/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGDICTIONARY_H_
#define _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGDICTIONARY_H_

#include "libimejyutping_export.h"
#include <libime/core/triedictionary.h>

namespace libime {
namespace jyutping {

enum class JyutpingDictFormat { Text, Binary };

class JyutpingDictionaryPrivate;

typedef std::function<bool(std::string_view encodedJyutping,
                           std::string_view hanzi, float cost)>
    JyutpingMatchCallback;
class JyutpingDictionary;

using JyutpingTrie = typename TrieDictionary::TrieType;

class LIBIMEJYUTPING_EXPORT JyutpingDictionary : public TrieDictionary {
public:
    static const size_t SystemDict = 0;
    static const size_t UserDict = 1;
    explicit JyutpingDictionary();
    ~JyutpingDictionary();

    // Load dicitonary for a specific dict.
    void load(size_t idx, std::istream &in, JyutpingDictFormat format);
    void load(size_t idx, const char *filename, JyutpingDictFormat format);

    // Match the word by encoded jyutping.
    void matchWords(const char *data, size_t size,
                    JyutpingMatchCallback callback) const;

    void save(size_t idx, const char *filename, JyutpingDictFormat format);
    void save(size_t idx, std::ostream &out, JyutpingDictFormat format);

    void addWord(size_t idx, std::string_view fullJyutping,
                 std::string_view hanzi, float cost = 0.0f);

    using dictionaryChanged = TrieDictionary::dictionaryChanged;

protected:
    void
    matchPrefixImpl(const SegmentGraph &graph,
                    const GraphMatchCallback &callback,
                    const std::unordered_set<const SegmentGraphNode *> &ignore,
                    void *helper) const override;

private:
    void loadText(size_t idx, std::istream &in);
    void loadBinary(size_t idx, std::istream &in);
    void saveText(size_t idx, std::ostream &out);

    std::unique_ptr<JyutpingDictionaryPrivate> d_ptr;
    FCITX_DECLARE_PRIVATE(JyutpingDictionary);
};

} // namespace jyutping
} // namespace libime

#endif // _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGDICTIONARY_H_
