/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGMATCHSTATE_P_H_
#define _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGMATCHSTATE_P_H_

#include <fcitx-utils/macros.h>
#include <libime/core/lattice.h>
#include <libime/core/lrucache.h>
#include <libime/jyutping/jyutpingdictionary.h>
#include <libime/jyutping/jyutpingmatchstate.h>
#include <memory>
#include <unordered_map>

namespace libime {

namespace jyutping {

using JyutpingTriePosition = std::pair<uint64_t, size_t>;
using JyutpingTriePositions = std::vector<JyutpingTriePosition>;

// Matching result for a specific JyutpingTrie.
struct MatchedJyutpingTrieNodes {
    MatchedJyutpingTrieNodes(const JyutpingTrie *trie, size_t size)
        : trie_(trie), size_(size) {}
    FCITX_INLINE_DEFINE_DEFAULT_DTOR_COPY_AND_MOVE(MatchedJyutpingTrieNodes)

    const JyutpingTrie *trie_;
    JyutpingTriePositions triePositions_;

    // Size of syllables.
    size_t size_;
};

// A cache to store the matched word, encoded Full Jyutping for this word and
// the
// adjustment score.
struct JyutpingMatchResult {
    JyutpingMatchResult(std::string_view s, float value,
                        std::string_view encodedJyutping)
        : word_(s, InvalidWordIndex), value_(value),
          encodedJyutping_(encodedJyutping) {}
    WordNode word_;
    float value_;
    std::string encodedJyutping_;
};

// class to store current SegmentGraphPath leads to this match and the match
// reuslt.
struct MatchedJyutpingPath {
    MatchedJyutpingPath(const JyutpingTrie *trie, size_t size,
                        SegmentGraphPath path)
        : result_(std::make_shared<MatchedJyutpingTrieNodes>(trie, size)),
          path_(std::move(path)) {}

    MatchedJyutpingPath(std::shared_ptr<MatchedJyutpingTrieNodes> result,
                        SegmentGraphPath path)
        : result_(result), path_(std::move(path)) {}

    FCITX_INLINE_DEFINE_DEFAULT_DTOR_COPY_AND_MOVE(MatchedJyutpingPath)

    auto &triePositions() { return result_->triePositions_; }
    const auto &triePositions() const { return result_->triePositions_; }
    const JyutpingTrie *trie() const { return result_->trie_; }

    // Size of syllables. not necessarily equal to size of path_, because there
    // may be separators.
    auto size() const { return result_->size_; }

    std::shared_ptr<MatchedJyutpingTrieNodes> result_;
    SegmentGraphPath path_;
};

// This need to be keep sync with JyutpingSegmentGraphPathHasher
class JyutpingStringHasher {
public:
    size_t operator()(const std::string &s) const {
        boost::hash<char> hasher;

        size_t seed = 0;
        for (char c : s) {
            boost::hash_combine(seed, hasher(c));
        }
        return seed;
    }
};

// A list of all search paths
typedef std::vector<MatchedJyutpingPath> MatchedJyutpingPaths;

// Map from SegmentGraphNode to Search Paths.
typedef std::unordered_map<const SegmentGraphNode *, MatchedJyutpingPaths>
    NodeToMatchedJyutpingPathsMap;

// A cache for all JyutpingTries. From a jyutping string to its matched
// JyutpingTrieNode
typedef std::unordered_map<
    const JyutpingTrie *,
    LRUCache<std::string, std::shared_ptr<MatchedJyutpingTrieNodes>,
             JyutpingStringHasher>>
    JyutpingTrieNodeCache;

// A cache for JyutpingMatchResult.
typedef std::unordered_map<
    const JyutpingTrie *,
    LRUCache<std::string, std::vector<JyutpingMatchResult>,
             JyutpingStringHasher>>
    JyutpingMatchResultCache;

class JyutpingMatchStatePrivate {
public:
    JyutpingMatchStatePrivate(JyutpingContext *context) : context_(context) {}

    JyutpingContext *context_;
    NodeToMatchedJyutpingPathsMap matchedPaths_;
    JyutpingTrieNodeCache nodeCacheMap_;
    JyutpingMatchResultCache matchCacheMap_;
};

} // namespace jyutping
} // namespace libime

#endif // _LIBIME_JYUTPING_LIBIME_JYUTPING_JYUTPINGMATCHSTATE_P_H_
