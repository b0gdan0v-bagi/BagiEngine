#include "ReplayFile.h"

#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
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
        String FindUnknownMember(JsonView object, std::initializer_list<StringView> known) {
            for (const JsonMember member : object.Members()) {
                bool found = false;
                for (const StringView name : known) {
                    found = found || member.key.TextEquals(name);
                }
                if (!found) {
                    String key;
                    member.key.ReadText(key);
                    return key;
                }
            }
            return {};
        }

        /// One line as a JSON object, taped into @p tape: within the size and depth limits of a request, which bound what a copy of the value costs
        std::expected<JsonView, ReplayError> ParseLineObject(StringView text, size_t line, JsonTape& tape) {
            if (text.size() > ControlService::kMaxRequestBytes) {
                return std::unexpected(MakeError(line, Format("the line is {} bytes, the limit is {}", text.size(), ControlService::kMaxRequestBytes)));
            }
            if (IsJsonNestedDeeperThan(text, ControlService::kMaxRequestDepth)) {
                return std::unexpected(MakeError(line, Format("the line nests deeper than {} levels", ControlService::kMaxRequestDepth)));
            }
            if (const auto parsed = tape.Parse(text); !parsed) {
                return std::unexpected(MakeError(line, Format("not JSON: {}", parsed.error().message)));
            }
            const JsonView root(tape, 0);
            if (!root.IsObject()) {
                return std::unexpected(MakeError(line, "the line has to be a JSON object"));
            }
            return root;
        }

        std::expected<String, ReplayError> ReadString(JsonView object, StringView key, size_t line) {
            String value;
            const Optional<JsonView> member = object.Find(key);
            if (!member || !member->ReadText(value)) {
                return std::unexpected(MakeError(line, Format("'{}' is missing or not a string", key)));
            }
            return value;
        }

        std::expected<ReplayHeader, ReplayError> ReadHeader(JsonView object, size_t line) {
            const Optional<JsonView> versionMember = object.Find(kKeyReplay);
            if (!versionMember) {
                return std::unexpected(MakeError(line, "the first line has to be the header with 'replay' (the format version)"));
            }
            const Optional<int64_t> version = versionMember->AsInt64();
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
            if (object.Find(kKeyTitle)) {
                auto title = ReadString(object, kKeyTitle, line);
                if (!title) {
                    return std::unexpected(title.error());
                }
                header.title = std::move(*title);
            }
            return header;
        }

        std::expected<ReplayGap, ReplayError> ReadGap(JsonView entry, size_t line) {
            const Optional<JsonView> gap = entry.Find(kKeyGap);
            if (!gap || !gap->IsObject()) {
                return std::unexpected(MakeError(line, "'gap' is missing or not an object"));
            }
            if (const String unknown = FindUnknownMember(*gap, {kKeyFrames, kKeySeconds}); !unknown.empty()) {
                return std::unexpected(MakeError(line, Format("'gap' has no member '{}'", unknown)));
            }
            const Optional<JsonView> framesMember = gap->Find(kKeyFrames);
            const Optional<int64_t> frames = framesMember ? framesMember->AsInt64() : std::nullopt;
            if (!frames) {
                return std::unexpected(MakeError(line, "'gap.frames' is missing or not an integer"));
            }
            if (*frames < 0) {
                return std::unexpected(MakeError(line, Format("'gap.frames' is negative ({})", *frames)));
            }
            const Optional<JsonView> secondsMember = gap->Find(kKeySeconds);
            const Optional<double> seconds = secondsMember ? secondsMember->AsDouble() : std::nullopt;
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

        /// @p value as compact JSON text
        String CompactText(JsonView value) {
            String text;
            JsonWriter writer(text);
            WriteJson(writer, value);
            return text;
        }

        std::expected<ReplayEntry, ReplayError> ReadEntry(JsonView object, size_t line) {
            if (const String unknown = FindUnknownMember(object, {kKeyOp, kKeyParams, kKeyOutcome, kKeyGap}); !unknown.empty()) {
                return std::unexpected(MakeError(line, Format("an entry has no member '{}'", unknown)));
            }
            String opName;
            const Optional<JsonView> opMember = object.Find(kKeyOp);
            if (!opMember || !opMember->ReadText(opName)) {
                return std::unexpected(MakeError(line, "'op' is missing or not a string"));
            }
            const Optional<ReplayOp> op = FindReplayOp(StringView(opName.c_str(), opName.size()));
            if (!op) {
                return std::unexpected(MakeError(line, Format("unknown op '{}' (it has to be reset, action or state)", opName)));
            }
            const Optional<JsonView> params = object.Find(kKeyParams);
            if (!params || !params->IsObject()) {
                return std::unexpected(MakeError(line, "'params' is missing or not an object"));
            }
            auto gap = ReadGap(object, line);
            if (!gap) {
                return std::unexpected(gap.error());
            }

            ReplayEntry entry;
            entry.op = *op;
            entry.params = CompactText(*params);
            if (const Optional<JsonView> outcome = object.Find(kKeyOutcome)) {
                entry.outcome = CompactText(*outcome);
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

        void WriteGap(JsonWriter& writer, const ReplayGap& gap) {
            writer.Key(kKeyGap);
            writer.BeginObject();
            writer.Member(kKeyFrames, gap.frames);
            writer.Member(kKeySeconds, gap.seconds);
            writer.EndObject();
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
        // The order is the file's own, not alphabetical: the writer keeps the order it is given
        String line;
        JsonWriter writer(line);
        writer.BeginObject();
        writer.Member(kKeyReplay, kReplayFormatVersion);
        writer.Member(kKeyGame, StringView(header.game));
        writer.Member(kKeyEngine, StringView(header.engine));
        if (!header.title.empty()) {
            writer.Member(kKeyTitle, StringView(header.title));
        }
        writer.EndObject();
        return line;
    }

    void AppendReplayEntry(String& out, const ReplayEntry& entry) {
        JsonWriter writer(out);
        writer.BeginObject();
        writer.Member(kKeyOp, ReplayOpName(entry.op));
        writer.Key(kKeyParams);
        writer.Raw(StringView(entry.params.c_str(), entry.params.size()));
        if (entry.outcome) {
            writer.Key(kKeyOutcome);
            writer.Raw(StringView(entry.outcome->c_str(), entry.outcome->size()));
        }
        WriteGap(writer, entry.gap);
        writer.EndObject();
    }

    void AppendReplayEntry(String& out, ReplayOp op, JsonView params, Optional<StringView> outcomeText, const ReplayGap& gap) {
        JsonWriter writer(out);
        writer.BeginObject();
        writer.Member(kKeyOp, ReplayOpName(op));
        writer.Key(kKeyParams);
        if (params.IsObject()) {
            WriteJson(writer, params);
        } else {
            writer.BeginObject();
            writer.EndObject();
        }
        if (outcomeText) {
            writer.Key(kKeyOutcome);
            writer.Raw(*outcomeText);
        }
        WriteGap(writer, gap);
        writer.EndObject();
    }

    String FormatReplayEntry(const ReplayEntry& entry) {
        String line;
        AppendReplayEntry(line, entry);
        return line;
    }

    std::expected<ParsedReplay, ReplayError> ParseReplay(StringView text) {
        ParsedReplay result;
        JsonTape tape;  // one for every line: the buffers keep their size
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

            auto object = ParseLineObject(line, lineNumber, tape);
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
