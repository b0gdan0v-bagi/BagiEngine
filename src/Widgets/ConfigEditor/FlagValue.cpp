#include "FlagValue.h"

#include <EASTL/algorithm.h>

namespace BECore {

    namespace {

        bool Contains(const Vector<String>& tokens, StringView token) {
            return eastl::find(tokens.begin(), tokens.end(), token) != tokens.end();
        }

    }  // namespace

    Vector<String> SplitFlagTokens(StringView value) {
        Vector<String> tokens;
        while (!value.empty()) {
            const auto pos = value.find('|');
            StringView token = (pos == StringView::npos) ? value : value.substr(0, pos);
            while (!token.empty() && token.front() == ' ') {
                token.remove_prefix(1);
            }
            while (!token.empty() && token.back() == ' ') {
                token.remove_suffix(1);
            }
            if (!token.empty()) {
                tokens.emplace_back(token.data(), token.size());
            }
            if (pos == StringView::npos) {
                break;
            }
            value = value.substr(pos + 1);
        }
        return tokens;
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
