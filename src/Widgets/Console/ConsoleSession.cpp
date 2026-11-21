#include "ConsoleSession.h"

#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <Widgets/Console/ConsoleCommandLine.h>
#include <Widgets/EditorLayout/EditorSettings.h>

namespace BECore {

    namespace {

        constexpr StringView kCutMark = "... (";

        bool IsBlank(char c) {
            return c == ' ' || c == '\t';
        }

        StringView Trim(StringView text) {
            while (!text.empty() && IsBlank(text.front())) {
                text.remove_prefix(1);
            }
            while (!text.empty() && IsBlank(text.back())) {
                text.remove_suffix(1);
            }
            return text;
        }

        bool IsUtf8Continuation(char c) {
            constexpr unsigned char kMask = 0xC0;
            constexpr unsigned char kContinuation = 0x80;
            return (static_cast<unsigned char>(c) & kMask) == kContinuation;
        }

        /// Prints the answer text of the control service: `result` as JSON, or `code: message`
        void PrintReply(ConsoleOutput& output, StringView replyText) {
            const auto reply = ParseJson(replyText);
            if (!reply) {
                output.Append(ConsoleOutputKind::Error, "the answer could not be read");
                return;
            }
            if (GetJsonBool(*reply, "ok").value_or(false)) {
                const Json* result = FindJsonMember(*reply, "result");
                output.Append(ConsoleOutputKind::Result, result != nullptr ? DumpJson(*result) : String("ok"));
                return;
            }
            const Json* error = FindJsonMember(*reply, "error");
            const String code = error != nullptr ? GetJsonString(*error, "code").value_or(String("error")) : String("error");
            const String message = error != nullptr ? GetJsonString(*error, "message").value_or(String()) : String();
            output.Append(ConsoleOutputKind::Error, Format("{}: {}", code, message));
        }

    }  // namespace

    void ConsoleOutput::Append(ConsoleOutputKind kind, StringView text) {
        StringView kept = text;
        String cutMark;
        if (text.size() > kMaxLineBytes) {
            size_t length = kMaxLineBytes;
            while (length > 0 && IsUtf8Continuation(text[length])) {
                --length;  // not in the middle of a character
            }
            kept = text.substr(0, length);
            cutMark = Format("{}{} more bytes)", kCutMark, text.size() - length);
        }

        ConsoleOutputLine* line = nullptr;
        if (_lines.size() < kMaxLines) {
            line = &_lines.emplace_back();
        } else {
            line = &_lines[_first];  // the oldest line makes room and gives its buffer to the new one
            _first = (_first + 1) % kMaxLines;
        }
        line->kind = kind;
        line->text.assign(kept.data(), kept.size());
        line->text += cutMark;
        ++_version;
    }

    void ConsoleOutput::Clear() {
        _lines.clear();
        _first = 0;
        ++_version;
    }

    void ConsoleCommandHistory::Set(const Vector<String>& entries) {
        _entries.clear();
        _walking = false;
        for (const String& entry : entries) {
            Add(entry);
        }
    }

    void ConsoleCommandHistory::Add(StringView command) {
        _walking = false;
        if (command.empty() || command.size() > kMaxConsoleCommandLength) {
            return;
        }
        for (size_t i = 0; i < _entries.size(); ++i) {
            if (_entries[i] == command) {
                _entries.erase(_entries.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
        }
        _entries.emplace_back(command);
        if (_entries.size() > kMaxConsoleHistory) {
            _entries.erase(_entries.begin());
        }
    }

    Optional<StringView> ConsoleCommandHistory::Previous(StringView typed) {
        if (_entries.empty()) {
            return std::nullopt;
        }
        if (!_walking) {
            _walking = true;
            _draft.assign(typed.data(), typed.size());
            _position = _entries.size();
        }
        if (_position > 0) {
            --_position;
        }
        return StringView(_entries[_position]);
    }

    Optional<StringView> ConsoleCommandHistory::Next() {
        if (!_walking) {
            return std::nullopt;
        }
        if (_position + 1 < _entries.size()) {
            ++_position;
            return StringView(_entries[_position]);
        }
        _walking = false;
        return StringView(_draft);
    }

    ConsoleSession::ConsoleSession() : _output(New<ConsoleOutput>()) {}

    Optional<String> ConsoleSession::Submit(ControlService& service, StringView text) {
        const StringView line = Trim(text);
        if (line.empty()) {
            return std::nullopt;
        }
        _output->Append(ConsoleOutputKind::Input, line);
        if (line.size() > kMaxConsoleCommandLength) {
            _output->Append(ConsoleOutputKind::Error, Format("the command is longer than {} bytes", kMaxConsoleCommandLength));
            return std::nullopt;
        }

        auto command = ParseConsoleCommand(line);
        if (!command) {
            _output->Append(ConsoleOutputKind::Error, command.error());
            return std::nullopt;
        }

        Json request = Json::object();
        request["id"] = _nextId++;
        request["method"] = ToStdString(command->method);
        request["params"] = std::move(command->params);
        service.Post(DumpJson(request), [output = _output](const String& answer) { PrintReply(*output, answer); });

        _history.Add(line);
        return String(line);
    }

}  // namespace BECore
