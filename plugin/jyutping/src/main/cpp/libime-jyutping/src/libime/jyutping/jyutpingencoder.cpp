/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "jyutpingencoder.h"
#include "jyutpingdata.h"
#include <boost/algorithm/string.hpp>
#include <boost/bimap.hpp>
#include <queue>

namespace libime {
namespace jyutping {

static const std::string emptyString;

template <typename L, typename R>
boost::bimap<L, R>
makeBimap(std::initializer_list<typename boost::bimap<L, R>::value_type> list) {
    return boost::bimap<L, R>(list.begin(), list.end());
}

static const auto initialMap = makeBimap<JyutpingInitial, std::string>({
    {JyutpingInitial::B, "b"},   {JyutpingInitial::P, "p"},
    {JyutpingInitial::M, "m"},   {JyutpingInitial::F, "f"},
    {JyutpingInitial::D, "d"},   {JyutpingInitial::T, "t"},
    {JyutpingInitial::N, "n"},   {JyutpingInitial::L, "l"},
    {JyutpingInitial::G, "g"},   {JyutpingInitial::K, "k"},
    {JyutpingInitial::NG, "ng"}, {JyutpingInitial::H, "h"},
    {JyutpingInitial::GW, "gw"}, {JyutpingInitial::KW, "kw"},
    {JyutpingInitial::W, "gw"},  {JyutpingInitial::W, "w"},
    {JyutpingInitial::Z, "z"},   {JyutpingInitial::C, "c"},
    {JyutpingInitial::S, "s"},   {JyutpingInitial::J, "j"},
    {JyutpingInitial::Zero, ""},
});

static const auto finalMap = makeBimap<JyutpingFinal, std::string>({
    {JyutpingFinal::AA, "aa"},   {JyutpingFinal::AAI, "aai"},
    {JyutpingFinal::AAU, "aau"}, {JyutpingFinal::AAM, "aam"},
    {JyutpingFinal::AAN, "aan"}, {JyutpingFinal::AANG, "aang"},
    {JyutpingFinal::AAP, "aap"}, {JyutpingFinal::AAT, "aat"},
    {JyutpingFinal::AAK, "aak"}, {JyutpingFinal::AI, "ai"},
    {JyutpingFinal::AU, "au"},   {JyutpingFinal::AM, "am"},
    {JyutpingFinal::AN, "an"},   {JyutpingFinal::ANG, "ang"},
    {JyutpingFinal::AP, "ap"},   {JyutpingFinal::AT, "at"},
    {JyutpingFinal::AK, "ak"},   {JyutpingFinal::E, "e"},
    {JyutpingFinal::EI, "ei"},   {JyutpingFinal::ET, "et"},
    {JyutpingFinal::EU, "eu"},   {JyutpingFinal::EM, "em"},
    {JyutpingFinal::EN, "en"},   {JyutpingFinal::ENG, "eng"},
    {JyutpingFinal::EP, "ep"},   {JyutpingFinal::EK, "ek"},
    {JyutpingFinal::I, "i"},     {JyutpingFinal::IU, "iu"},
    {JyutpingFinal::IM, "im"},   {JyutpingFinal::IN, "in"},
    {JyutpingFinal::ING, "ing"}, {JyutpingFinal::IP, "ip"},
    {JyutpingFinal::IT, "it"},   {JyutpingFinal::IK, "ik"},
    {JyutpingFinal::O, "o"},     {JyutpingFinal::OI, "oi"},
    {JyutpingFinal::OU, "ou"},   {JyutpingFinal::OM, "om"},
    {JyutpingFinal::ON, "on"},   {JyutpingFinal::ONG, "ong"},
    {JyutpingFinal::OT, "ot"},   {JyutpingFinal::OK, "ok"},
    {JyutpingFinal::OE, "oe"},   {JyutpingFinal::OENG, "oeng"},
    {JyutpingFinal::OEK, "oek"}, {JyutpingFinal::EOI, "eoi"},
    {JyutpingFinal::EON, "eon"}, {JyutpingFinal::EOT, "eot"},
    {JyutpingFinal::U, "u"},     {JyutpingFinal::UI, "ui"},
    {JyutpingFinal::UN, "un"},   {JyutpingFinal::UNG, "ung"},
    {JyutpingFinal::UT, "ut"},   {JyutpingFinal::UK, "uk"},
    {JyutpingFinal::YU, "yu"},   {JyutpingFinal::YUN, "yun"},
    {JyutpingFinal::YUT, "yut"}, {JyutpingFinal::M, "m"},
    {JyutpingFinal::NG, "ng"},   {JyutpingFinal::Zero, ""},
});

static const int maxJyutpingLength = 6;

template <typename Iter>
std::pair<std::string_view, bool> longestMatch(Iter iter, Iter end) {
    if (std::distance(iter, end) > maxJyutpingLength) {
        end = iter + maxJyutpingLength;
    }
    auto range = std::string_view(&*iter, std::distance(iter, end));
    auto &map = getJyutpingMap();
    for (; range.size(); range.remove_suffix(1)) {
        auto iterPair = map.equal_range(range);
        if (iterPair.first != iterPair.second) {
            // do not consider m/ng as complete jyutping
            return std::make_pair(range, (range != "m" && range != "ng"));
        }
        if (range.size() <= 2) {
            auto iter = initialMap.right.find(std::string{range});
            if (iter != initialMap.right.end()) {
                return std::make_pair(range, false);
            }
        }
    }

    if (!range.size()) {
        range = std::string_view(&*iter, 1);
    }

    return std::make_pair(range, false);
}

std::string JyutpingSyllable::toString() const {
    return JyutpingEncoder::initialToString(initial_) +
           JyutpingEncoder::finalToString(final_);
}

SegmentGraph JyutpingEncoder::parseUserJyutping(std::string userJyutping,
                                                bool inner) {
    SegmentGraph result(std::move(userJyutping));
    const auto &jyutping = result.data();
    auto end = jyutping.end();
    std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> q;
    q.push(0);
    while (q.size()) {
        size_t top;
        do {
            top = q.top();
            q.pop();
        } while (q.size() && q.top() == top);
        if (top >= jyutping.size()) {
            continue;
        }
        auto iter = std::next(jyutping.begin(), top);
        if (*iter == '\'') {
            while (*iter == '\'' && iter != jyutping.end()) {
                iter++;
            }
            auto next = std::distance(jyutping.begin(), iter);
            result.addNext(top, next);
            if (static_cast<size_t>(next) < jyutping.size()) {
                q.push(next);
            }
            continue;
        }
        std::string_view str;
        bool isCompleteJyutping;
        std::tie(str, isCompleteJyutping) = longestMatch(iter, end);

        // it's not complete a jyutping, no need to try
        if (!isCompleteJyutping) {
            result.addNext(top, top + str.size());
            q.push(top + str.size());
        } else {
            // check fuzzy seg
            // jyutping may end with aegikmnoptu
            // and may start with abcdefghjklmnopstuwz.
            // the intersection is aegkmnoptu.
            // also, make sure current jyutping does not end with a separator.
            auto &map = getJyutpingMap();
            std::array<size_t, 2> nextSize;
            size_t nNextSize = 0;
            if (str.size() > 1 && top + str.size() < jyutping.size() &&
                jyutping[top + str.size()] != '\'' &&
                (str.back() == 'a' || str.back() == 'e' || str.back() == 'g' ||
                 str.back() == 'k' || str.back() == 'm' || str.back() == 'n' ||
                 str.back() == 'o' || str.back() == 'p' || str.back() == 't' ||
                 str.back() == 'u') &&
                map.find(str.substr(0, str.size() - 1)) != map.end()) {
                // str[0:-1] is also a full jyutping, check next jyutping
                auto nextMatch = longestMatch(iter + str.size(), end);
                auto nextMatchAlt = longestMatch(iter + str.size() - 1, end);
                auto matchSize = str.size() + nextMatch.first.size();
                auto matchSizeAlt = str.size() - 1 + nextMatchAlt.first.size();
                if (std::make_pair(matchSize, nextMatch.second) >=
                    std::make_pair(matchSizeAlt, nextMatchAlt.second)) {
                    result.addNext(top, top + str.size());
                    q.push(top + str.size());
                    nextSize[nNextSize++] = str.size();
                }
                if (std::make_pair(matchSize, nextMatch.second) <=
                    std::make_pair(matchSizeAlt, nextMatchAlt.second)) {
                    result.addNext(top, top + str.size() - 1);
                    q.push(top + str.size() - 1);
                    nextSize[nNextSize++] = str.size() - 1;
                }
            } else {
                result.addNext(top, top + str.size());
                q.push(top + str.size());
                nextSize[nNextSize++] = str.size();
            }

            for (size_t i = 0; i < nNextSize; i++) {
                if (nextSize[i] >= 4 && inner) {
                    auto &innerSegments = getInnerSegment();
                    auto iter = innerSegments.find(
                        std::string{str.substr(0, nextSize[i])});
                    if (iter != innerSegments.end()) {
                        result.addNext(top, top + iter->second.first.size());
                        result.addNext(top + iter->second.first.size(),
                                       top + nextSize[i]);
                    }
                }
            }
        }
    }
    return result;
}

std::vector<char> JyutpingEncoder::encodeOneUserJyutping(std::string jyutping) {
    if (jyutping.empty()) {
        return {};
    }
    auto graph = parseUserJyutping(std::move(jyutping), false);
    std::vector<char> result;
    const SegmentGraphNode *node = &graph.start(), *prev = nullptr;
    while (node->nextSize()) {
        prev = node;
        node = &node->nexts().front();
        auto seg = graph.segment(*prev, *node);
        if (seg.empty() || seg[0] == '\'') {
            continue;
        }
        auto syls = stringToSyllables(seg);
        if (!syls.size()) {
            return {};
        }
        result.push_back(static_cast<char>(syls[0].first));
        result.push_back(static_cast<char>(syls[0].second[0].first));
    }
    return result;
}

bool JyutpingEncoder::isValidUserJyutping(const char *data, size_t size) {
    if (size % 2 != 0) {
        return false;
    }

    for (size_t i = 0; i < size / 2; i++) {
        if (!JyutpingEncoder::isValidInitial(data[i * 2])) {
            return false;
        }
    }
    return true;
}

std::string JyutpingEncoder::decodeFullJyutping(const char *data, size_t size) {
    if (size % 2 != 0) {
        throw std::invalid_argument("invalid jyutping key");
    }
    std::string result;
    for (size_t i = 0, e = size / 2; i < e; i++) {
        if (i) {
            result += '\'';
        }
        result += initialToString(static_cast<JyutpingInitial>(data[i * 2]));
        result += finalToString(static_cast<JyutpingFinal>(data[i * 2 + 1]));
    }
    return result;
}

const std::string &JyutpingEncoder::initialToString(JyutpingInitial initial) {
    const static std::vector<std::string> s = []() {
        std::vector<std::string> s;
        s.resize(lastInitial - firstInitial + 1);
        for (char c = firstInitial; c <= lastInitial; c++) {
            auto iter = initialMap.left.find(static_cast<JyutpingInitial>(c));
            s[c - firstInitial] = iter->second;
        }
        return s;
    }();
    auto c = static_cast<char>(initial);
    if (c >= firstInitial && c <= lastInitial) {
        return s[c - firstInitial];
    }
    return emptyString;
}

JyutpingInitial JyutpingEncoder::stringToInitial(const std::string &str) {
    auto iter = initialMap.right.find(str);
    if (iter != initialMap.right.end()) {
        return iter->second;
    }
    return JyutpingInitial::Invalid;
}

const std::string &JyutpingEncoder::finalToString(JyutpingFinal final) {
    const static std::vector<std::string> s = []() {
        std::vector<std::string> s;
        s.resize(lastFinal - firstFinal + 1);
        for (char c = firstFinal; c <= lastFinal; c++) {
            auto iter = finalMap.left.find(static_cast<JyutpingFinal>(c));
            s[c - firstFinal] = iter->second;
        }
        return s;
    }();
    auto c = static_cast<char>(final);
    if (c >= firstFinal && c <= lastFinal) {
        return s[c - firstFinal];
    }
    return emptyString;
}

JyutpingFinal JyutpingEncoder::stringToFinal(const std::string &str) {
    auto iter = finalMap.right.find(str);
    if (iter != finalMap.right.end()) {
        return iter->second;
    }
    return JyutpingFinal::Invalid;
}

bool JyutpingEncoder::isValidInitialFinal(JyutpingInitial initial,
                                          JyutpingFinal final) {
    if (initial != JyutpingInitial::Invalid &&
        final != JyutpingFinal::Invalid) {
        int16_t encode =
            ((static_cast<int16_t>(initial) - JyutpingEncoder::firstInitial) *
             (JyutpingEncoder::lastFinal - JyutpingEncoder::firstFinal + 1)) +
            (static_cast<int16_t>(final) - JyutpingEncoder::firstFinal);
        const auto &a = getEncodedInitialFinal();
        return encode < static_cast<int>(a.size()) && a[encode];
    }
    return false;
}

static void getFuzzy(
    std::vector<std::pair<JyutpingInitial,
                          std::vector<std::pair<JyutpingFinal, bool>>>> &syls,
    JyutpingSyllable syl) {
    JyutpingInitial initials[2] = {syl.initial(), JyutpingInitial::Invalid};
    JyutpingFinal finals[2] = {syl.final(), JyutpingFinal::Invalid};
    int initialSize = 1;
    int finalSize = 1;

    for (int i = 0; i < initialSize; i++) {
        for (int j = 0; j < finalSize; j++) {
            auto initial = initials[i];
            auto final = finals[j];
            if ((i == 0 && j == 0) || final == JyutpingFinal::Invalid ||
                JyutpingEncoder::isValidInitialFinal(initial, final)) {
                auto iter = std::find_if(
                    syls.begin(), syls.end(),
                    [initial](const auto &p) { return p.first == initial; });
                if (iter == syls.end()) {
                    syls.emplace_back(std::piecewise_construct,
                                      std::forward_as_tuple(initial),
                                      std::forward_as_tuple());
                    iter = std::prev(syls.end());
                }
                auto &finals = iter->second;
                if (std::find_if(finals.begin(), finals.end(),
                                 [final](auto &p) {
                                     return p.first == final;
                                 }) == finals.end()) {
                    finals.emplace_back(final, i > 0 || j > 0);
                }
            }
        }
    }
}

MatchedJyutpingSyllables
JyutpingEncoder::stringToSyllables(std::string_view jyutping) {
    std::vector<
        std::pair<JyutpingInitial, std::vector<std::pair<JyutpingFinal, bool>>>>
        result;
    auto &map = getJyutpingMap();
    // we only want {M,N,R}/Invalid instead of {M,N,R}/Zero, so we could get
    // match for everything.
    if (jyutping != "m" && jyutping != "ng") {
        auto iterPair = map.equal_range(jyutping);
        for (auto &item :
             boost::make_iterator_range(iterPair.first, iterPair.second)) {
            getFuzzy(result, {item.initial(), item.final()});
        }
    }

    auto iter = initialMap.right.find(std::string{jyutping});
    if (initialMap.right.end() != iter) {
        getFuzzy(result, {iter->second, JyutpingFinal::Invalid});
    }

    if (result.size() == 0) {
        result.emplace_back(
            std::piecewise_construct,
            std::forward_as_tuple(JyutpingInitial::Invalid),
            std::forward_as_tuple(
                1, std::make_pair(JyutpingFinal::Invalid, false)));
    }

    return result;
}

std::vector<char>
JyutpingEncoder::encodeFullJyutping(std::string_view jyutping) {
    std::vector<std::string> jyutpings;
    boost::split(jyutpings, jyutping, boost::is_any_of("'"));
    std::vector<char> result;
    result.resize(jyutpings.size() * 2);
    int idx = 0;
    for (const auto &singleJyutping : jyutpings) {
        auto &map = getJyutpingMap();
        auto iter = map.find(singleJyutping);
        if (iter == map.end()) {
            throw std::invalid_argument("invalid full jyutping: " +
                                        std::string{jyutping});
        }
        result[idx++] = static_cast<char>(iter->initial());
        result[idx++] = static_cast<char>(iter->final());
    }

    return result;
}

} // namespace jyutping
} // namespace libime
