#include "CommandFilter.h"

namespace BECore {

    namespace {

        constexpr int kNoMatch = -1;
        constexpr int kCharScore = 1;
        constexpr int kWordStartBonus = 8;
        constexpr int kRunBonus = 4;
        constexpr int kPrefixBonus = 16;
        constexpr int kTitleWeight = 2;

        char ToLowerAscii(char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool IsWordChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        }

        // The first character of the text, of a word after a separator, or an upper case letter after a lower case one
        // ("commandPalette" has two words)
        bool IsWordStart(StringView text, size_t index) {
            if (index == 0) {
                return true;
            }
            const char previous = text[index - 1];
            const char current = text[index];
            if (!IsWordChar(previous)) {
                return true;
            }
            return previous >= 'a' && previous <= 'z' && current >= 'A' && current <= 'Z';
        }

        // Best score of matching @p query (lower case, no spaces) as a subsequence of @p text, or kNoMatch. Tries every
        // placement (a short text and query, so the table of best scores stays small).
        int ScoreMatch(StringView query, StringView text) {
            if (query.size() > text.size()) {
                return kNoMatch;
            }
            // best[j]: best score with the current query character matched at text[j]
            eastl::vector<int> best(text.size(), kNoMatch);
            for (size_t qi = 0; qi < query.size(); ++qi) {
                eastl::vector<int> next(text.size(), kNoMatch);
                for (size_t j = 0; j < text.size(); ++j) {
                    if (ToLowerAscii(text[j]) != query[qi]) {
                        continue;
                    }
                    const int here = kCharScore + (IsWordStart(text, j) ? kWordStartBonus : 0);
                    if (qi == 0) {
                        next[j] = here;
                        continue;
                    }
                    int bestBefore = kNoMatch;
                    for (size_t k = 0; k < j; ++k) {
                        if (best[k] == kNoMatch) {
                            continue;
                        }
                        bestBefore = std::max(bestBefore, best[k] + (k + 1 == j ? kRunBonus : 0));
                    }
                    if (bestBefore != kNoMatch) {
                        next[j] = bestBefore + here;
                    }
                }
                best = std::move(next);
            }
            int result = kNoMatch;
            for (const int score : best) {
                result = std::max(result, score);
            }
            return result;
        }

        bool StartsWithNoCase(StringView text, StringView prefix) {
            if (prefix.size() > text.size()) {
                return false;
            }
            for (size_t i = 0; i < prefix.size(); ++i) {
                if (ToLowerAscii(text[i]) != prefix[i]) {
                    return false;
                }
            }
            return true;
        }

        String NormalizeQuery(StringView query) {
            String normalized;
            for (const char c : query) {
                if (c != ' ') {
                    normalized.push_back(ToLowerAscii(c));
                }
            }
            return normalized;
        }

    }  // namespace

    eastl::vector<const EditorCommand*> FilterCommands(StringView query, eastl::span<const EditorCommand> commands) {
        const String normalized = NormalizeQuery(query);

        eastl::vector<const EditorCommand*> result;
        if (normalized.empty()) {
            for (const EditorCommand& command : commands) {
                result.push_back(&command);
            }
            return result;
        }

        struct Hit {
            const EditorCommand* command;
            int score;
        };
        eastl::vector<Hit> hits;
        const StringView needle(normalized.data(), normalized.size());
        for (const EditorCommand& command : commands) {
            int titleScore = ScoreMatch(needle, command.title);
            if (titleScore != kNoMatch && StartsWithNoCase(command.title, needle)) {
                titleScore += kPrefixBonus;
            }
            const int idScore = ScoreMatch(needle, command.id.ToStringView());
            const int score = std::max(titleScore == kNoMatch ? kNoMatch : titleScore * kTitleWeight, idScore);
            if (score != kNoMatch) {
                hits.push_back({&command, score});
            }
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit& lhs, const Hit& rhs) { return lhs.score > rhs.score; });
        for (const Hit& hit : hits) {
            result.push_back(hit.command);
        }
        return result;
    }

    int MoveSelection(int selected, int delta, int count) {
        if (count <= 0) {
            return 0;
        }
        const int clamped = std::clamp(selected, 0, count - 1);
        return ((clamped + delta) % count + count) % count;
    }

}  // namespace BECore
