#include "FlagValue.h"

#include <EASTL/algorithm.h>

namespace BECore {

    namespace {

        bool Contains(const Vector<String>& tokens, StringView token) {
            return eastl::find(tokens.begin(), tokens.end(), token) != tokens.end();
        }

        // The next token of `value` (trimmed, possibly empty) and the rest after its bar
        StringView TakeToken(StringView& value) {
            const auto pos = value.find('|');
            StringView token = (pos == StringView::npos) ? value : value.substr(0, pos);
            value = (pos == StringView::npos) ? StringView{} : value.substr(pos + 1);
            while (!token.empty() && token.front() == ' ') {
                token.remove_prefix(1);
            }
            while (!token.empty() && token.back() == ' ') {
                token.remove_suffix(1);
            }
            return token;
        }

    }  // namespace

    Vector<String> SplitFlagTokens(StringView value) {
        Vector<String> tokens;
        while (!value.empty()) {
            const StringView token = TakeToken(value);
            if (!token.empty()) {
                tokens.emplace_back(token.data(), token.size());
            }
        }
        return tokens;
    }

    bool HasFlagToken(StringView value, StringView token) {
        while (!value.empty()) {
            const StringView current = TakeToken(value);
            if (!current.empty() && current == token) {
                return true;
            }
        }
        return false;
    }

    void ForEachUnknownFlagToken(StringView value, const Vector<String>& options, FunctionRef<void(StringView)> fn) {
        const StringView all = value;
        while (!value.empty()) {
            const StringView token = TakeToken(value);
            if (token.empty() || Contains(options, token)) {
                continue;
            }
            // A token seen earlier in the value was already reported: look at the text before this one
            const StringView before(all.data(), static_cast<size_t>(token.data() - all.data()));
            if (!HasFlagToken(before, token)) {
                fn(token);
            }
        }
    }

    Vector<String> UnknownFlagTokens(StringView value, const Vector<String>& options) {
        Vector<String> unknown;
        for (String& token : SplitFlagTokens(value)) {
            if (!Contains(options, token) && !Contains(unknown, token)) {
                unknown.push_back(std::move(token));
            }
        }
        return unknown;
    }

    String JoinFlagTokens(const Vector<String>& tokens) {
        String result;
        for (const String& token : tokens) {
            if (!result.empty()) {
                result.push_back('|');
            }
            result.append(token);
        }
        return result;
    }

}  // namespace BECore
