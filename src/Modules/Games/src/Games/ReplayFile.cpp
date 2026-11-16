#include "ReplayFile.h"

#include <BECore/Control/ControlService.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr StringView kOpReset = "reset";
        constexpr StringView kOpAction = "action";
        constexpr StringView kOpState = "state";

        constexpr StringView kKeyReplay = "replay";
        constexpr StringView kKeyGame = "game";
        constexpr StringView kKeyEngine = "engine";
        constexpr StringView kKeyTitle = "title";
        constexpr StringView kKeyOp = "op";
        constexpr StringView kKeyParams = "params";
        constexpr StringView kKeyOutcome = "outcome";
        constexpr StringView kKeyGap = "gap";
        constexpr StringView kKeyFrames = "frames";
        constexpr StringView kKeySeconds = "seconds";

        ReplayError MakeError(size_t line, String message) {
            return ReplayError{line, std::move(message)};
        }

        /// The first member of @p object that is not in @p known, as text for an error, or an empty string
        String FindUnknownMember(const Json& object, std::initializer_list<StringView> known) {
            for (const auto& member : object.items()) {
                const StringView key(member.key().c_str(), member.key().size());
                bool found = false;
                for (const StringView name : known) {
                    found = found || name == key;
                }
                if (!found) {
                    return String(key);
                }
            }
            return {};
        }

        /// One line as a JSON object: within the size and depth limits of a request, which bound what a copy of the value costs
        std::expected<Json, ReplayError> ParseLineObject(StringView text, size_t line) {
            if (text.size() > ControlService::kMaxRequestBytes) {
                return std::unexpected(MakeError(line, Format("the line is {} bytes, the limit is {}", text.size(), ControlService::kMaxRequestBytes)));
            }
            if (IsJsonNestedDeeperThan(text, ControlService::kMaxRequestDepth)) {
                return std::unexpected(MakeError(line, Format("the line nests deeper than {} levels", ControlService::kMaxRequestDepth)));
            }
            auto parsed = ParseJson(text);
            if (!parsed) {
                return std::unexpected(MakeError(line, Format("not JSON: {}", parsed.error().message)));
            }
            if (!parsed->is_object()) {
                return std::unexpected(MakeError(line, "the line has to be a JSON object"));
            }
            return std::move(*parsed);
        }

        std::expected<String, ReplayError> ReadString(const Json& object, StringView key, size_t line) {
            const auto value = GetJsonString(object, key);
            if (!value) {
                return std::unexpected(MakeError(line, Format("'{}' is missing or not a string", key)));
            }
            return String(value->c_str(), value->size());
        }

        std::expected<ReplayHeader, ReplayError> ReadHeader(const Json& object, size_t line) {
            if (FindJsonMember(object, kKeyReplay) == nullptr) {
                return std::unexpected(MakeError(line, "the first line has to be the header with 'replay' (the format version)"));
            }
            const auto version = GetJsonInt(object, kKeyReplay);
            if (!version) {
                return std::unexpected(MakeError(line, "'replay' has to be an integer"));
            }
            if (*version != kReplayFormatVersion) {
                return std::unexpected(MakeError(line, Format("unknown replay version {}, this build reads {}", *version, kReplayFormatVersion)));
            }
            if (const String unknown = FindUnknownMember(object, {kKeyReplay, kKeyGame, kKeyEngine, kKeyTitle}); !unknown.empty()) {
                return std::unexpected(MakeError(line, Format("the header has no member '{}'", unknown)));
            }

            ReplayHeader header;
            auto game = ReadString(object, kKeyGame, line);
            if (!game) {
                return std::unexpected(game.error());
            }
            auto engine = ReadString(object, kKeyEngine, line);
            if (!engine) {
                return std::unexpected(engine.error());
            }
            header.game = std::move(*game);
            header.engine = std::move(*engine);
            if (FindJsonMember(object, kKeyTitle) != nullptr) {
                auto title = ReadString(object, kKeyTitle, line);
                if (!title) {
                    return std::unexpected(title.error());
                }
                header.title = std::move(*title);
            }
            return header;
        }

        std::expected<ReplayGap, ReplayError> ReadGap(const Json& entry, size_t line) {
            const Json* gap = FindJsonMember(entry, kKeyGap);
            if (gap == nullptr || !gap->is_object()) {
                return std::unexpected(MakeError(line, "'gap' is missing or not an object"));
            }
            if (const String unknown = FindUnknownMember(*gap, {kKeyFrames, kKeySeconds}); !unknown.empty()) {
                return std::unexpected(MakeError(line, Format("'gap' has no member '{}'", unknown)));
            }
            const auto frames = GetJsonInt(*gap, kKeyFrames);
            if (!frames) {
                return std::unexpected(MakeError(line, "'gap.frames' is missing or not an integer"));
            }
            if (*frames < 0) {
                return std::unexpected(MakeError(line, Format("'gap.frames' is negative ({})", *frames)));
            }
            const auto seconds = GetJsonNumber(*gap, kKeySeconds);
            if (!seconds) {
                return std::unexpected(MakeError(line, "'gap.seconds' is missing or not a number"));
            }
            if (!std::isfinite(*seconds) || *seconds < 0.0) {
                return std::unexpected(MakeError(line, "'gap.seconds' has to be a finite number, not negative"));
            }
            if (*frames == 0 && *seconds != 0.0) {
                return std::unexpected(MakeError(line, "'gap' has seconds but no frames: time only passes in frames"));
            }
            return ReplayGap{static_cast<uint64_t>(*frames), *seconds};
        }

        std::expected<ReplayEntry, ReplayError> ReadEntry(const Json& object, size_t line) {
            if (const String unknown = FindUnknownMember(object, {kKeyOp, kKeyParams, kKeyOutcome, kKeyGap}); !unknown.empty()) {
                return std::unexpected(MakeError(line, Format("an entry has no member '{}'", unknown)));
            }
            const auto opName = GetJsonString(object, kKeyOp);
            if (!opName) {
                return std::unexpected(MakeError(line, "'op' is missing or not a string"));
            }
            const Optional<ReplayOp> op = FindReplayOp(StringView(opName->c_str(), opName->size()));
            if (!op) {
                return std::unexpected(MakeError(line, Format("unknown op '{}' (it has to be reset, action or state)", *opName)));
            }
            const Json* params = FindJsonMember(object, kKeyParams);
            if (params == nullptr || !params->is_object()) {
                return std::unexpected(MakeError(line, "'params' is missing or not an object"));
            }
            auto gap = ReadGap(object, line);
            if (!gap) {
                return std::unexpected(gap.error());
            }

            ReplayEntry entry;
            entry.op = *op;
            entry.params = *params;
            if (const Json* outcome = FindJsonMember(object, kKeyOutcome)) {
                entry.outcome = *outcome;
            }
            entry.gap = *gap;
            entry.line = line;
            return entry;
        }

        /// The text without a trailing '\r', so a file saved on Windows reads the same
        StringView TrimLineEnd(StringView text) {
            if (!text.empty() && text.back() == '\r') {
                text.remove_suffix(1);
            }
            return text;
        }

        bool IsBlank(StringView text) {
            return text.find_first_not_of(" \t") == StringView::npos;
        }

    }  // namespace

    StringView ReplayOpName(ReplayOp op) {
        switch (op) {
            case ReplayOp::Reset:
                return kOpReset;
            case ReplayOp::Action:
                return kOpAction;
            case ReplayOp::State:
                return kOpState;
        }
        return kOpReset;
    }

    Optional<ReplayOp> FindReplayOp(StringView name) {
        for (const ReplayOp op : {ReplayOp::Reset, ReplayOp::Action, ReplayOp::State}) {
            if (ReplayOpName(op) == name) {
                return op;
            }
        }
        return std::nullopt;
    }

    String DescribeReplayError(const ReplayError& error) {
        return Format("line {}: {}", error.line, error.message);
    }

    String FormatReplayHeader(const ReplayHeader& header) {
        String line;
        AppendFormat(line, "{{\"{}\":{},\"{}\":{},\"{}\":{}", kKeyReplay, kReplayFormatVersion, kKeyGame, DumpJson(MakeJsonString(header.game)), kKeyEngine, DumpJson(MakeJsonString(header.engine)));
        if (!header.title.empty()) {
            AppendFormat(line, ",\"{}\":{}", kKeyTitle, DumpJson(MakeJsonString(header.title)));
        }
        line.push_back('}');
        return line;
    }

    String FormatReplayEntry(const ReplayEntry& entry) {
        // The order is written by hand: a Json object keeps its members sorted by name
        String line;
        AppendFormat(line, "{{\"{}\":\"{}\",\"{}\":{}", kKeyOp, ReplayOpName(entry.op), kKeyParams, DumpJson(entry.params));
        if (entry.outcome) {
            AppendFormat(line, ",\"{}\":{}", kKeyOutcome, DumpJson(*entry.outcome));
        }
        AppendFormat(line, ",\"{}\":{{\"{}\":{},\"{}\":{}}}}}", kKeyGap, kKeyFrames, entry.gap.frames, kKeySeconds, DumpJson(Json(entry.gap.seconds)));
        return line;
    }

    std::expected<ParsedReplay, ReplayError> ParseReplay(StringView text) {
        ParsedReplay result;
        bool haveHeader = false;
        size_t lineNumber = 0;

        size_t position = 0;
        while (position <= text.size()) {
            const size_t end = std::min(text.find('\n', position), text.size());
            const StringView line = TrimLineEnd(text.substr(position, end - position));
            position = end + 1;
            ++lineNumber;
            if (IsBlank(line)) {
                continue;
            }

            auto object = ParseLineObject(line, lineNumber);
            if (!object) {
                return std::unexpected(object.error());
            }
            if (!haveHeader) {
                auto header = ReadHeader(*object, lineNumber);
                if (!header) {
                    return std::unexpected(header.error());
                }
                result.header = std::move(*header);
                haveHeader = true;
                continue;
            }

            auto entry = ReadEntry(*object, lineNumber);
            if (!entry) {
                return std::unexpected(entry.error());
            }
            if (result.entries.empty() && entry->op != ReplayOp::Reset) {
                return std::unexpected(MakeError(lineNumber, "the first entry has to be a 'reset': the file starts from a start"));
            }
            result.entries.push_back(std::move(*entry));
        }

        if (!haveHeader) {
            return std::unexpected(MakeError(1, "the file has no header line"));
        }
        return result;
    }

}  // namespace BECore
