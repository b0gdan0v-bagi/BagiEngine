#include "ConsoleCommandLine.h"

#include <BECore/Control/ControlService.h>

namespace BECore {

    namespace {

        /// A parameters object sits one level below the request object that carries it
        constexpr size_t kMaxParamsDepth = ControlService::kMaxRequestDepth - 1;

        bool IsBlank(char c) {
            return c == ' ' || c == '\t';
        }

        bool IsNameChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        }

        bool IsKeyChar(char c) {
            return IsNameChar(c) || c == '-';
        }

        bool AllOf(StringView text, bool (*accepts)(char)) {
            if (text.empty()) {
                return false;
            }
            for (const char c : text) {
                if (!accepts(c)) {
                    return false;
                }
            }
            return true;
        }

        size_t SkipBlanks(StringView text, size_t from) {
            while (from < text.size() && IsBlank(text[from])) {
                ++from;
            }
            return from;
        }

        struct Word {
            size_t begin = 0;
            size_t end = 0;
        };

        /// The words of @p text split on blanks that are outside double quotes and outside balanced `{}` and `[]`
        struct WordScan {
            Vector<Word> words;
            bool open = false;  ///< the text ends inside a quote or a bracket
            String error;       ///< a closing bracket that matches nothing
        };

        WordScan ScanWords(StringView text) {
            WordScan scan;
            String closers;  // the closing brackets still expected, innermost last
            size_t i = SkipBlanks(text, 0);
            while (i < text.size() && scan.error.empty()) {
                Word word;
                word.begin = i;
                bool inQuote = false;
                closers.clear();
                while (i < text.size() && (inQuote || !closers.empty() || !IsBlank(text[i]))) {
                    const char c = text[i];
                    if (inQuote) {
                        if (c == '\\' && i + 1 < text.size()) {
                            ++i;
                        } else if (c == '"') {
                            inQuote = false;
                        }
                    } else if (c == '"') {
                        inQuote = true;
                    } else if (c == '{') {
                        closers.push_back('}');
                    } else if (c == '[') {
                        closers.push_back(']');
                    } else if (c == '}' || c == ']') {
                        if (closers.empty() || closers.back() != c) {
                            scan.error = Format("unbalanced '{}'", c);
                            break;
                        }
                        closers.pop_back();
                    }
                    ++i;
                }
                word.end = i;
                scan.words.push_back(word);
                if (inQuote || !closers.empty()) {
                    scan.open = true;
                }
                i = SkipBlanks(text, i);
            }
            return scan;
        }

        /// Reads one `key=value` word into @p params
        std::expected<void, String> ReadAssignment(StringView word, Json& params) {
            const size_t equals = word.find('=');
            if (equals == StringView::npos) {
                return std::unexpected(Format("'{}' is not key=value", word));
            }
            const StringView key = word.substr(0, equals);
            const StringView valueText = word.substr(equals + 1);
            if (key.empty()) {
                return std::unexpected(Format("'{}' has no key before '='", word));
            }
            if (!AllOf(key, IsKeyChar)) {
                return std::unexpected(Format("'{}' is not a parameter name", key));
            }
            if (FindJsonMember(params, key) != nullptr) {
                return std::unexpected(Format("'{}' is given twice", key));
            }
            if (IsJsonNestedDeeperThan(valueText, kMaxParamsDepth)) {
                return std::unexpected(Format("the value of '{}' is nested too deep", key));
            }
            Json value = MakeJsonString(valueText);
            if (auto parsed = ParseJson(valueText)) {
                if (parsed->is_null()) {
                    return std::unexpected(Format("'{}' cannot be null: leave it out", key));
                }
                value = std::move(*parsed);
            }
            params[ToStdString(key)] = std::move(value);
            return {};
        }

        size_t CommonPrefixLength(const Vector<StringView>& names) {
            if (names.empty()) {
                return 0;
            }
            size_t length = names.front().size();
            for (const StringView name : names) {
                size_t same = 0;
                while (same < length && same < name.size() && name[same] == names.front()[same]) {
                    ++same;
                }
                length = same;
            }
            return length;
        }

        bool StartsWith(StringView text, StringView prefix) {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        /// The completion of @p matches for the word that starts at @p position of @p text: one match is finished with @p suffix, several give their common prefix
        void ApplyMatches(StringView text, size_t position, const Vector<StringView>& matches, StringView suffix, ConsoleCompletion& result) {
            if (matches.empty()) {
                return;
            }
            result.text.assign(text.data(), position);
            if (matches.size() == 1) {
                result.text.append(matches.front().data(), matches.front().size());
                result.text.append(suffix.data(), suffix.size());
                return;
            }
            const size_t common = CommonPrefixLength(matches);
            result.text.append(matches.front().data(), common);
        }

        ConsoleCompletion CompleteMethodName(StringView text, size_t start, Span<const ControlMethodListing> methods) {
            ConsoleCompletion result{String(text), {}};
            const StringView prefix = text.substr(start);
            Vector<StringView> matches;
            for (const ControlMethodListing& listing : methods) {
                if (StartsWith(listing.name, prefix)) {
                    matches.push_back(listing.name);
                }
            }
            ApplyMatches(text, start, matches, " ", result);
            if (matches.size() > 1) {
                for (const StringView name : matches) {
                    result.candidates.emplace_back(name);
                }
            }
            return result;
        }

        String DescribeParam(const ControlParamInfo& param) {
            String text = param.name;
            if (!param.type.empty() || param.required) {
                text += " (";
                text += param.type;
                if (param.required) {
                    text += param.type.empty() ? "required" : ", required";
                }
                text += ")";
            }
            return text;
        }

        ConsoleCompletion CompleteParamName(StringView text, size_t methodEnd, const ControlMethodInfo& info) {
            ConsoleCompletion result{String(text), {}};
            const StringView rest = text.substr(methodEnd);
            const size_t restStart = SkipBlanks(rest, 0);
            if (info.params.empty() || (restStart < rest.size() && rest[restStart] == '{')) {
                return result;
            }
            const WordScan scan = ScanWords(rest);
            if (scan.open || !scan.error.empty()) {
                return result;
            }

            // The word under the cursor is the last one when the text does not end with a blank
            size_t position = text.size();
            StringView prefix;
            size_t given = scan.words.size();
            if (!scan.words.empty() && scan.words.back().end == rest.size()) {
                const Word& last = scan.words.back();
                prefix = rest.substr(last.begin, last.end - last.begin);
                if (prefix.find('=') != StringView::npos) {
                    return result;  // a finished key=value is not extended
                }
                position = methodEnd + last.begin;
                --given;
            }

            Vector<const ControlParamInfo*> matches;
            for (const bool wantRequired : {true, false}) {
                for (const ControlParamInfo& param : info.params) {
                    if (param.required != wantRequired || !StartsWith(param.name, prefix)) {
                        continue;
                    }
                    bool isGiven = false;
                    for (size_t i = 0; i < given && !isGiven; ++i) {
                        const StringView word = rest.substr(scan.words[i].begin, scan.words[i].end - scan.words[i].begin);
                        isGiven = StartsWith(word, param.name) && word.size() > param.name.size() && word[param.name.size()] == '=';
                    }
                    if (!isGiven) {
                        matches.push_back(&param);
                    }
                }
            }

            Vector<StringView> names;
            for (const ControlParamInfo* param : matches) {
                names.push_back(param->name);
            }
            ApplyMatches(text, position, names, "=", result);
            if (matches.size() > 1) {
                for (const ControlParamInfo* param : matches) {
                    result.candidates.push_back(DescribeParam(*param));
                }
            }
            return result;
        }

    }  // namespace

    std::expected<ConsoleCommand, String> ParseConsoleCommand(StringView text) {
        const size_t start = SkipBlanks(text, 0);
        size_t methodEnd = start;
        while (methodEnd < text.size() && !IsBlank(text[methodEnd])) {
            ++methodEnd;
        }
        const StringView method = text.substr(start, methodEnd - start);
        if (method.empty()) {
            return std::unexpected(String("type a method name"));
        }
        if (!AllOf(method, IsNameChar)) {
            return std::unexpected(Format("'{}' is not a method name (letters, digits, '_' and '.')", method));
        }

        auto params = ParseConsoleParams(text.substr(methodEnd));
        if (!params) {
            return std::unexpected(std::move(params.error()));
        }
        return ConsoleCommand{String(method), std::move(*params)};
    }

    std::expected<Json, String> ParseConsoleParams(StringView text) {
        const size_t restStart = SkipBlanks(text, 0);
        size_t restEnd = text.size();
        while (restEnd > restStart && IsBlank(text[restEnd - 1])) {
            --restEnd;
        }
        const StringView rest = text.substr(restStart, restEnd - restStart);
        if (rest.empty()) {
            return Json::object();
        }

        if (rest.front() == '{') {
            if (IsJsonNestedDeeperThan(rest, kMaxParamsDepth)) {
                return std::unexpected(String("the parameters are nested too deep"));
            }
            auto parsed = ParseJson(rest);
            if (!parsed) {
                return std::unexpected(Format("the parameters are not valid JSON: {}", parsed.error().message));
            }
            if (!parsed->is_object()) {
                return std::unexpected(String("the parameters have to be a JSON object"));
            }
            return std::move(*parsed);
        }

        const WordScan scan = ScanWords(rest);
        if (!scan.error.empty()) {
            return std::unexpected(scan.error);
        }
        if (scan.open) {
            return std::unexpected(String("a quote or a bracket is not closed"));
        }
        Json params = Json::object();
        for (const Word& word : scan.words) {
            if (auto read = ReadAssignment(rest.substr(word.begin, word.end - word.begin), params); !read) {
                return std::unexpected(std::move(read.error()));
            }
        }
        return params;
    }

    ConsoleCompletion CompleteConsoleCommand(StringView text, Span<const ControlMethodListing> methods) {
        const size_t start = SkipBlanks(text, 0);
        size_t wordEnd = start;
        while (wordEnd < text.size() && !IsBlank(text[wordEnd])) {
            ++wordEnd;
        }
        if (wordEnd == text.size()) {
            return CompleteMethodName(text, start, methods);
        }

        const StringView method = text.substr(start, wordEnd - start);
        for (const ControlMethodListing& listing : methods) {
            if (listing.name == method && listing.info != nullptr) {
                return CompleteParamName(text, wordEnd, *listing.info);
            }
        }
        return ConsoleCompletion{String(text), {}};
    }

}  // namespace BECore
