/*
 * SPDX-FileCopyrightText: 2018~2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "jyutpingdictionary.h"
#include "jyutpingdecoder_p.h"
#include "jyutpingencoder.h"
#include "jyutpingmatchstate_p.h"
#include "libime/core/datrie.h"
#include "libime/core/lattice.h"
#include "libime/core/lrucache.h"
#include "utils_p.h"
#include "zstdfilter.h"
#include <boost/algorithm/string.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/unordered_map.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <istream>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace libime {
namespace jyutping {

static const float fuzzyCost = std::log10(0.5f);
static const float invalidJyutpingCost = -100.0f;
static const char jyutpingHanziSep = '\x01';

static constexpr uint32_t jyutpingBinaryFormatMagic = 0x000fc733;
static constexpr uint32_t jyutpingBinaryFormatVersion = 0x2;

struct JyutpingSegmentGraphPathHasher {
    JyutpingSegmentGraphPathHasher(const SegmentGraph &graph) : graph_(graph) {}

    // Generate a "|" separated raw jyutping string from given path, skip all
    // separator.
    std::string pathToJyutpings(const SegmentGraphPath &path) const {
        std::string result;
        result.reserve(path.size() + path.back()->index() -
                       path.front()->index() + 1);
        const auto &data = graph_.data();
        auto iter = path.begin();
        while (iter + 1 < path.end()) {
            auto begin = (*iter)->index();
            auto end = (*std::next(iter))->index();
            iter++;
            if (data[begin] == '\'') {
                continue;
            }
            while (begin < end) {
                result.push_back(data[begin]);
                begin++;
            }
            result.push_back('|');
        }
        return result;
    }

    // Generate hash for path but avoid allocate the string.
    size_t operator()(const SegmentGraphPath &path) const {
        if (path.size() <= 1) {
            return 0;
        }
        boost::hash<char> hasher;

        size_t seed = 0;
        const auto &data = graph_.data();
        auto iter = path.begin();
        while (iter + 1 < path.end()) {
            auto begin = (*iter)->index();
            auto end = (*std::next(iter))->index();
            iter++;
            if (data[begin] == '\'') {
                continue;
            }
            while (begin < end) {
                boost::hash_combine(seed, hasher(data[begin]));
                begin++;
            }
            boost::hash_combine(seed, hasher('|'));
        }
        return seed;
    }

    // Check equality of jyutping string and the path. The string s should be
    // equal to pathToJyutpings(path), but this function just try to avoid
    // allocate a string for comparisin.
    bool operator()(const SegmentGraphPath &path, const std::string &s) const {
        if (path.size() <= 1) {
            return false;
        }
        auto is = s.begin();
        const auto &data = graph_.data();
        auto iter = path.begin();
        while (iter + 1 < path.end() && is != s.end()) {
            auto begin = (*iter)->index();
            auto end = (*std::next(iter))->index();
            iter++;
            if (data[begin] == '\'') {
                continue;
            }
            while (begin < end && is != s.end()) {
                if (*is != data[begin]) {
                    return false;
                }
                is++;
                begin++;
            }
            if (begin != end) {
                return false;
            }

            if (is == s.end() || *is != '|') {
                return false;
            }
            is++;
        }
        return iter + 1 == path.end() && is == s.end();
    }

private:
    const SegmentGraph &graph_;
};

struct SegmentGraphNodeGreater {
    bool operator()(const SegmentGraphNode *lhs,
                    const SegmentGraphNode *rhs) const {
        return lhs->index() > rhs->index();
    }
};

// Check if the prev not is a jyutping. Separator always contrains in its own
// segment.
const SegmentGraphNode *prevIsSeparator(const SegmentGraph &graph,
                                        const SegmentGraphNode &node) {
    if (node.prevSize() == 1) {
        auto &prev = node.prevs().front();
        auto jyutping = graph.segment(prev, node);
        if (boost::starts_with(jyutping, "\'")) {
            return &prev;
        }
    }
    return nullptr;
}

class JyutpingMatchContext {
public:
    explicit JyutpingMatchContext(
        const SegmentGraph &graph, const GraphMatchCallback &callback,
        const std::unordered_set<const SegmentGraphNode *> &ignore,
        JyutpingMatchState *matchState)
        : graph_(graph), hasher_(graph), callback_(callback), ignore_(ignore),
          matchedPathsMap_(&matchState->d_func()->matchedPaths_),
          nodeCacheMap_(&matchState->d_func()->nodeCacheMap_),
          matchCacheMap_(&matchState->d_func()->matchCacheMap_) {}

    explicit JyutpingMatchContext(
        const SegmentGraph &graph, const GraphMatchCallback &callback,
        const std::unordered_set<const SegmentGraphNode *> &ignore,
        NodeToMatchedJyutpingPathsMap &matchedPaths)
        : graph_(graph), hasher_(graph), callback_(callback), ignore_(ignore),
          matchedPathsMap_(&matchedPaths) {}

    FCITX_INLINE_DEFINE_DEFAULT_DTOR_AND_COPY(JyutpingMatchContext);

    const SegmentGraph &graph_;
    JyutpingSegmentGraphPathHasher hasher_;

    const GraphMatchCallback &callback_;
    const std::unordered_set<const SegmentGraphNode *> &ignore_;
    NodeToMatchedJyutpingPathsMap *matchedPathsMap_;
    JyutpingTrieNodeCache *nodeCacheMap_ = nullptr;
    JyutpingMatchResultCache *matchCacheMap_ = nullptr;
};

class JyutpingDictionaryPrivate : fcitx::QPtrHolder<JyutpingDictionary> {
public:
    JyutpingDictionaryPrivate(JyutpingDictionary *q)
        : fcitx::QPtrHolder<JyutpingDictionary>(q) {}

    void addEmptyMatch(const JyutpingMatchContext &context,
                       const SegmentGraphNode &currentNode,
                       MatchedJyutpingPaths &currentMatches) const;

    void findMatchesBetween(const JyutpingMatchContext &context,
                            const SegmentGraphNode &prevNode,
                            const SegmentGraphNode &currentNode,
                            MatchedJyutpingPaths &currentMatches) const;

    bool matchWords(const JyutpingMatchContext &context,
                    const MatchedJyutpingPaths &newPaths) const;
    bool matchWordsForOnePath(const JyutpingMatchContext &context,
                              const MatchedJyutpingPath &path) const;

    void matchNode(const JyutpingMatchContext &context,
                   const SegmentGraphNode &currentNode) const;
};

void JyutpingDictionaryPrivate::addEmptyMatch(
    const JyutpingMatchContext &context, const SegmentGraphNode &currentNode,
    MatchedJyutpingPaths &currentMatches) const {
    FCITX_Q();
    const SegmentGraph &graph = context.graph_;
    // Create a new starting point for current node, and put it in matchResult.
    if (&currentNode != &graph.end() &&
        !boost::starts_with(
            graph.segment(currentNode.index(), currentNode.index() + 1),
            "\'")) {
        SegmentGraphPath vec;
        if (auto prev = prevIsSeparator(graph, currentNode)) {
            vec.push_back(prev);
        }

        vec.push_back(&currentNode);
        for (size_t i = 0; i < q->dictSize(); i++) {
            auto &trie = *q->trie(i);
            currentMatches.emplace_back(&trie, 0, vec);
            currentMatches.back().triePositions().emplace_back(0, 0);
        }
    }
}

JyutpingTriePositions
traverseAlongPathOneStepBySyllables(const MatchedJyutpingPath &path,
                                    const MatchedJyutpingSyllables &syls) {
    JyutpingTriePositions positions;
    for (const auto &pr : path.triePositions()) {
        uint64_t _pos;
        size_t fuzzies;
        std::tie(_pos, fuzzies) = pr;
        for (auto &syl : syls) {
            // make a copy
            auto pos = _pos;
            auto initial = static_cast<char>(syl.first);
            auto result = path.trie()->traverse(&initial, 1, pos);
            if (JyutpingTrie::isNoPath(result)) {
                continue;
            }
            const auto &finals = syl.second;

            auto updateNext = [fuzzies, &path,
                               &positions](char final, int fuzzy, auto pos) {
                auto result = path.trie()->traverse(&final, 1, pos);

                if (!JyutpingTrie::isNoPath(result)) {
                    size_t newFuzzies = fuzzies + fuzzy;
                    positions.emplace_back(pos, newFuzzies);
                }
            };
            if (finals.size() > 1 ||
                finals[0].first != JyutpingFinal::Invalid) {
                for (auto final : finals) {
                    updateNext(static_cast<char>(final.first), 0, pos);
                }
            } else {
                // Assign a different factory for "m" and "ng", since these
                // character can only be matched with "m" or "ng".
                int fuzzyFactor = JyutpingEncoder::isValidInitialFinal(
                                      syl.first, JyutpingFinal::Zero)
                                      ? 10
                                      : 1;
                for (char test = JyutpingEncoder::firstFinal;
                     test <= JyutpingEncoder::lastFinal; test++) {
                    auto curFinal = static_cast<JyutpingFinal>(test);
                    if (JyutpingEncoder::isValidInitialFinal(syl.first,
                                                             curFinal)) {
                        updateNext(
                            test,
                            (curFinal == JyutpingFinal::Zero ? 0 : fuzzyFactor),
                            pos);
                    }
                }
            }
        }
    }
    return positions;
}

template <typename T>
void matchWordsOnTrie(const MatchedJyutpingPath &path, const T &callback) {
    const char sep = jyutpingHanziSep;
    for (auto &pr : path.triePositions()) {
        uint64_t pos;
        size_t fuzzies;
        std::tie(pos, fuzzies) = pr;
        float extraCost = fuzzies * fuzzyCost;
        auto result = path.trie()->traverse(&sep, 1, pos);
        if (JyutpingTrie::isNoPath(result)) {
            continue;
        }

        path.trie()->foreach(
            [&path, &callback, extraCost](JyutpingTrie::value_type value,
                                          size_t len, uint64_t pos) {
                std::string s;
                s.reserve(len + path.size() * 2 + 1);
                path.trie()->suffix(s, len + path.size() * 2 + 1, pos);
                std::string_view view(s);
                auto encodedJyutping = view.substr(0, path.size() * 2);
                auto hanzi = view.substr(path.size() * 2 + 1);
                callback(encodedJyutping, hanzi, value + extraCost);
                return true;
            },
            pos);
    }
}

bool JyutpingDictionaryPrivate::matchWordsForOnePath(
    const JyutpingMatchContext &context,
    const MatchedJyutpingPath &path) const {
    bool matched = false;
    assert(path.path_.size() >= 2);
    const SegmentGraphNode &prevNode = *path.path_[path.path_.size() - 2];
    if (context.matchCacheMap_) {
        auto &matchCache = (*context.matchCacheMap_)[path.trie()];
        auto result =
            matchCache.find(path.path_, context.hasher_, context.hasher_);
        if (!result) {
            result =
                matchCache.insert(context.hasher_.pathToJyutpings(path.path_));
            result->clear();

            auto &items = *result;
            matchWordsOnTrie(path, [&items](std::string_view encodedJyutping,
                                            std::string_view hanzi,
                                            float cost) {
                items.emplace_back(hanzi, cost, encodedJyutping);
            });
        }
        for (auto &item : *result) {
            context.callback_(path.path_, item.word_, item.value_,
                              std::make_unique<JyutpingLatticeNodePrivate>(
                                  item.encodedJyutping_));
            if (path.size() == 1 &&
                path.path_[path.path_.size() - 2] == &prevNode) {
                matched = true;
            }
        }
    } else {
        matchWordsOnTrie(path, [&matched, &path, &context,
                                &prevNode](std::string_view encodedJyutping,
                                           std::string_view hanzi, float cost) {
            WordNode word(hanzi, InvalidWordIndex);
            context.callback_(
                path.path_, word, cost,
                std::make_unique<JyutpingLatticeNodePrivate>(encodedJyutping));
            if (path.size() == 1 &&
                path.path_[path.path_.size() - 2] == &prevNode) {
                matched = true;
            }
        });
    }

    return matched;
}

bool JyutpingDictionaryPrivate::matchWords(
    const JyutpingMatchContext &context,
    const MatchedJyutpingPaths &newPaths) const {
    bool matched = false;
    for (const auto &path : newPaths) {
        matched |= matchWordsForOnePath(context, path);
    }

    return matched;
}

void JyutpingDictionaryPrivate::findMatchesBetween(
    const JyutpingMatchContext &context, const SegmentGraphNode &prevNode,
    const SegmentGraphNode &currentNode,
    MatchedJyutpingPaths &currentMatches) const {
    const SegmentGraph &graph = context.graph_;
    auto &matchedPathsMap = *context.matchedPathsMap_;
    auto jyutping = graph.segment(prevNode, currentNode);
    // If predecessor is a separator, just copy every existing match result
    // over and don't traverse on the trie.
    if (boost::starts_with(jyutping, "\'")) {
        const auto &prevMatches = matchedPathsMap[&prevNode];
        for (auto &match : prevMatches) {
            // copy the path, and append current node.
            auto path = match.path_;
            path.push_back(&currentNode);
            currentMatches.emplace_back(match.result_, std::move(path));
        }
        // If the last segment is separator, there
        if (&currentNode == &graph.end()) {
            WordNode word("", 0);
            context.callback_({&prevNode, &currentNode}, word, 0, nullptr);
        }
        return;
    }

    const auto syls = JyutpingEncoder::stringToSyllables(jyutping);
    const MatchedJyutpingPaths &prevMatchedPaths = matchedPathsMap[&prevNode];
    MatchedJyutpingPaths newPaths;
    for (auto &path : prevMatchedPaths) {
        // Make a copy of path so we can modify based on it.
        auto segmentPath = path.path_;
        segmentPath.push_back(&currentNode);

        if (context.nodeCacheMap_) {
            auto &nodeCache = (*context.nodeCacheMap_)[path.trie()];
            auto p =
                nodeCache.find(segmentPath, context.hasher_, context.hasher_);
            std::shared_ptr<MatchedJyutpingTrieNodes> result;
            if (!p) {
                result = std::make_shared<MatchedJyutpingTrieNodes>(
                    path.trie(), path.size() + 1);
                nodeCache.insert(context.hasher_.pathToJyutpings(segmentPath),
                                 result);
                result->triePositions_ =
                    traverseAlongPathOneStepBySyllables(path, syls);
            } else {
                result = *p;
                assert(result->size_ == path.size() + 1);
            }

            if (result->triePositions_.size()) {
                newPaths.emplace_back(result, segmentPath);
            }
        } else {
            // make an empty one
            newPaths.emplace_back(path.trie(), path.size() + 1, segmentPath);

            newPaths.back().result_->triePositions_ =
                traverseAlongPathOneStepBySyllables(path, syls);
            // if there's nothing, pop it.
            if (!newPaths.back().triePositions().size()) {
                newPaths.pop_back();
            }
        }
    }

    if (!context.ignore_.count(&currentNode)) {
        // after we match current syllable, we first try to match word.
        if (!matchWords(context, newPaths)) {
            // If we failed to match any length 1 word, add a new empty word
            // to make lattice connect together.
            SegmentGraphPath vec;
            vec.reserve(3);
            if (auto prevPrev = prevIsSeparator(context.graph_, prevNode)) {
                vec.push_back(prevPrev);
            }
            vec.push_back(&prevNode);
            vec.push_back(&currentNode);
            WordNode word(jyutping, InvalidWordIndex);
            context.callback_(vec, word, invalidJyutpingCost, nullptr);
        }
    }

    std::move(newPaths.begin(), newPaths.end(),
              std::back_inserter(currentMatches));
}

void JyutpingDictionaryPrivate::matchNode(
    const JyutpingMatchContext &context,
    const SegmentGraphNode &currentNode) const {
    auto &matchedPathsMap = *context.matchedPathsMap_;
    // Check if the node has been searched already.
    if (matchedPathsMap.count(&currentNode)) {
        return;
    }
    auto &currentMatches = matchedPathsMap[&currentNode];
    // To create a new start.
    addEmptyMatch(context, currentNode, currentMatches);

    // Iterate all predecessor and search from them.
    for (auto &prevNode : currentNode.prevs()) {
        findMatchesBetween(context, prevNode, currentNode, currentMatches);
    }
}

void JyutpingDictionary::matchPrefixImpl(
    const SegmentGraph &graph, const GraphMatchCallback &callback,
    const std::unordered_set<const SegmentGraphNode *> &ignore,
    void *helper) const {
    FCITX_D();

    NodeToMatchedJyutpingPathsMap localMatchedPaths;
    JyutpingMatchContext context =
        helper
            ? JyutpingMatchContext{graph, callback, ignore,
                                   static_cast<JyutpingMatchState *>(helper)}
            : JyutpingMatchContext{graph, callback, ignore, localMatchedPaths};

    // A queue to make sure that node with smaller index will be visted first
    // because we want to make sure every predecessor node are visited before
    // visit the current node.
    using SegmentGraphNodeQueue =
        std::priority_queue<const SegmentGraphNode *,
                            std::vector<const SegmentGraphNode *>,
                            SegmentGraphNodeGreater>;
    SegmentGraphNodeQueue q;

    auto &start = graph.start();
    q.push(&start);

    while (!q.empty()) {
        auto currentNode = q.top();
        q.pop();

        // Push successors into the queue.
        for (auto &node : currentNode->nexts()) {
            q.push(&node);
        }

        d->matchNode(context, *currentNode);
    }
}

void JyutpingDictionary::matchWords(const char *data, size_t size,
                                    JyutpingMatchCallback callback) const {
    if (!JyutpingEncoder::isValidUserJyutping(data, size)) {
        return;
    }

    std::list<std::pair<const JyutpingTrie *, JyutpingTrie::position_type>>
        nodes;
    for (size_t i = 0; i < dictSize(); i++) {
        auto &trie = *this->trie(i);
        nodes.emplace_back(&trie, 0);
    }
    for (size_t i = 0; i <= size && nodes.size(); i++) {
        char current;
        if (i < size) {
            current = data[i];
        } else {
            current = jyutpingHanziSep;
        }
        decltype(nodes) extraNodes;
        auto iter = nodes.begin();
        while (iter != nodes.end()) {
            if (current != 0) {
                JyutpingTrie::value_type result;
                result = iter->first->traverse(&current, 1, iter->second);

                if (JyutpingTrie::isNoPath(result)) {
                    nodes.erase(iter++);
                } else {
                    iter++;
                }
            } else {
                bool changed = false;
                for (char test = JyutpingEncoder::firstFinal;
                     test <= JyutpingEncoder::lastFinal; test++) {
                    decltype(extraNodes)::value_type p = *iter;
                    auto result = p.first->traverse(&test, 1, p.second);
                    if (!JyutpingTrie::isNoPath(result)) {
                        extraNodes.push_back(p);
                        changed = true;
                    }
                }
                if (changed) {
                    *iter = extraNodes.back();
                    extraNodes.pop_back();
                    iter++;
                } else {
                    nodes.erase(iter++);
                }
            }
        }
        nodes.splice(nodes.end(), std::move(extraNodes));
    }

    for (auto &node : nodes) {
        node.first->foreach(
            [&node, &callback, size](JyutpingTrie::value_type value, size_t len,
                                     uint64_t pos) {
                std::string s;
                node.first->suffix(s, len + size + 1, pos);

                auto view = std::string_view(s);
                return callback(s.substr(0, size), view.substr(size + 1),
                                value);
            },
            node.second);
    }
}
JyutpingDictionary::JyutpingDictionary()
    : d_ptr(std::make_unique<JyutpingDictionaryPrivate>(this)) {
    addEmptyDict();
    addEmptyDict();
}

JyutpingDictionary::~JyutpingDictionary() {}

void JyutpingDictionary::load(size_t idx, const char *filename,
                              JyutpingDictFormat format) {
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (!in) {
        throw std::ios_base::failure("io fail");
    }
    load(idx, in, format);
}

void JyutpingDictionary::load(size_t idx, std::istream &in,
                              JyutpingDictFormat format) {
    switch (format) {
    case JyutpingDictFormat::Text:
        loadText(idx, in);
        break;
    case JyutpingDictFormat::Binary:
        loadBinary(idx, in);
        break;
    default:
        throw std::invalid_argument("invalid format type");
    }
    emit<JyutpingDictionary::dictionaryChanged>(idx);
}

void JyutpingDictionary::loadText(size_t idx, std::istream &in) {
    DATrie<float> trie;

    std::string buf;
    auto isSpaceCheck = boost::is_any_of(" \n\t\r\v\f");
    while (!in.eof()) {
        if (!std::getline(in, buf)) {
            break;
        }

        boost::trim_if(buf, isSpaceCheck);
        std::vector<std::string> tokens;
        boost::split(tokens, buf, isSpaceCheck);
        if (tokens.size() == 3) {
            const std::string &hanzi = tokens[0];
            std::string_view jyutping = tokens[1];
            float prob = std::stof(tokens[2]);
            auto result = JyutpingEncoder::encodeFullJyutping(jyutping);
            result.push_back(jyutpingHanziSep);
            result.insert(result.end(), hanzi.begin(), hanzi.end());
            trie.set(result.data(), result.size(), prob);
        }
    }
    *mutableTrie(idx) = std::move(trie);
}

void JyutpingDictionary::loadBinary(size_t idx, std::istream &in) {
    DATrie<float> trie;
    uint32_t magic;
    uint32_t version;
    throw_if_io_fail(unmarshall(in, magic));
    if (magic != jyutpingBinaryFormatMagic) {
        throw std::invalid_argument("Invalid jyutping magic.");
    }
    throw_if_io_fail(unmarshall(in, version));
    switch (version) {
    case 0x1:
        trie.load(in);
        break;
    case jyutpingBinaryFormatVersion: {
        boost::iostreams::filtering_istreambuf compressBuf;
        compressBuf.push(ZSTDDecompressor());
        compressBuf.push(in);
        std::istream compressIn(&compressBuf);

        trie.load(compressIn);
        // We don't want to read any data, but only trigger the zstd footer
        // handling, which validates CRC.
        compressIn.peek();
        if (compressIn.bad()) {
            throw std::invalid_argument("Failed to load dict data");
        }
        break;
    }
    default:
        throw std::invalid_argument("Invalid jyutping version.");
    }
    *mutableTrie(idx) = std::move(trie);
}

void JyutpingDictionary::save(size_t idx, const char *filename,
                              JyutpingDictFormat format) {
    std::ofstream fout(filename, std::ios::out | std::ios::binary);
    if (!fout) {
        throw std::ios_base::failure("io fail");
    }
    save(idx, fout, format);
}

void JyutpingDictionary::save(size_t idx, std::ostream &out,
                              JyutpingDictFormat format) {
    switch (format) {
    case JyutpingDictFormat::Text:
        saveText(idx, out);
        break;
    case JyutpingDictFormat::Binary: {
        throw_if_io_fail(marshall(out, jyutpingBinaryFormatMagic));
        throw_if_io_fail(marshall(out, jyutpingBinaryFormatVersion));
        boost::iostreams::filtering_streambuf<boost::iostreams::output>
            compressBuf;
        compressBuf.push(ZSTDCompressor());
        compressBuf.push(out);
        std::ostream compressOut(&compressBuf);
        mutableTrie(idx)->save(compressOut);
        break;
    }
    default:
        throw std::invalid_argument("invalid format type");
    }
}

void JyutpingDictionary::saveText(size_t idx, std::ostream &out) {
    std::string buf;
    std::ios state(nullptr);
    state.copyfmt(out);
    auto &trie = *this->trie(idx);
    trie.foreach([this, &trie, &buf, &out](float value, size_t _len,
                                           JyutpingTrie::position_type pos) {
        trie.suffix(buf, _len, pos);
        auto sep = buf.find(jyutpingHanziSep);
        if (sep == std::string::npos) {
            return true;
        }
        std::string_view ref(buf);
        auto fullJyutping =
            JyutpingEncoder::decodeFullJyutping(ref.data(), sep);
        out << ref.substr(sep + 1) << " " << fullJyutping << " "
            << std::setprecision(16) << value << std::endl;
        return true;
    });
    out.copyfmt(state);
}

void JyutpingDictionary::addWord(size_t idx, std::string_view fullJyutping,
                                 std::string_view hanzi, float cost) {
    auto result = JyutpingEncoder::encodeFullJyutping(fullJyutping);
    result.push_back(jyutpingHanziSep);
    result.insert(result.end(), hanzi.begin(), hanzi.end());
    TrieDictionary::addWord(idx, std::string_view(result.data(), result.size()),
                            cost);
}
} // namespace jyutping
} // namespace libime
