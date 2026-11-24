#include "CommandLineDeserializer.h"

#include <BECore/Reflection/ReaderLimits.h>
#include <BECore/Reflection/StrictText.h>

namespace BECore {

    namespace {

        constexpr StringView kFlagPrefix = "--";
        constexpr char kWordSeparator = '-';
        constexpr StringView kNeedsValue = "needs a value";
        constexpr StringView kHoldsOneValue = "a flag holds one value";

        bool IsUpperCase(char c) {
            return c >= 'A' && c <= 'Z';
        }

        char ToLowerCase(char c) {
            return static_cast<char>(c - 'A' + 'a');
        }

    }  // namespace

    StringView MakeFlagName(StringView key, FlagBuffer& buffer) {
        size_t length = 0;
        const auto append = [&](char c) {
            if (length >= buffer.size()) {
                return false;
            }
            buffer[length++] = c;
            return true;
        };

        for (const char c : kFlagPrefix) {
            if (!append(c)) {
                return {};
            }
        }
        for (const char c : key) {
            const bool fits = IsUpperCase(c) ? append(kWordSeparator) && append(ToLowerCase(c)) : append(c);
            if (!fits) {
                return {};
            }
        }
        return StringView(buffer.data(), length);
    }

    CommandLineDeserializer::CommandLineDeserializer(const CommandLine& line, Span<const StringView> keys) : _line(line), _keys(keys) {}

    bool CommandLineDeserializer::IsFlagKey(StringView key) const {
        if (_keys.empty()) {
            return true;
        }
        for (const StringView listed : _keys) {
            if (listed == key) {
                return true;
            }
        }
        return false;
    }

    Optional<CommandLineArgument> CommandLineDeserializer::Lookup(StringView key) {
        if (!IsFlagKey(key)) {
            return std::nullopt;
        }
        FlagBuffer buffer;
        const StringView flag = MakeFlagName(key, buffer);
        if (flag.empty()) {
            return std::nullopt;
        }
        return _line.Find(flag);
    }

    bool CommandLineDeserializer::HasKey(StringView name) const {
        if (!IsFlagKey(name)) {
            return false;
        }
        FlagBuffer buffer;
        const StringView flag = MakeFlagName(name, buffer);
        return !flag.empty() && _line.Find(flag).has_value();
    }

    Optional<StringView> CommandLineDeserializer::RequireValue(StringView key, const CommandLineArgument& argument) {
        if (!argument.hasValue) {
            ReportError(key, kNeedsValue);
            return std::nullopt;
        }
        return argument.value;
    }

    template <typename T>
    bool CommandLineDeserializer::ReadFlag(StringView key, T& outValue) {
        const Optional<CommandLineArgument> argument = Lookup(key);
        if (!argument) {
            return false;
        }
        if constexpr (std::is_same_v<T, bool>) {
            if (!argument->hasValue) {
                outValue = true;
                return true;
            }
        }
        const Optional<StringView> text = RequireValue(key, *argument);
        if (!text) {
            return false;
        }
        const StrictText::Status status = StrictText::Parse(*text, outValue);
        if (status != StrictText::Status::Ok) {
            const StringView shown = CutText(*text, ReaderLimits::kMaxQuotedValueBytes);
            ReportError(key, StrictText::InvalidValueMessage<T>(*text, shown, status));
            return false;
        }
        return true;
    }

    bool CommandLineDeserializer::ReadText(StringView key, String& outValue) {
        const Optional<CommandLineArgument> argument = Lookup(key);
        if (!argument) {
            return false;
        }
        const Optional<StringView> text = RequireValue(key, *argument);
        if (!text) {
            return false;
        }
        outValue.assign(text->data(), text->size());
        return true;
    }

    bool CommandLineDeserializer::ReadPoolText(StringView key, PoolString& outValue) {
        const Optional<CommandLineArgument> argument = Lookup(key);
        if (!argument) {
            return false;
        }
        const Optional<StringView> text = RequireValue(key, *argument);
        if (!text || !AcceptPoolStringLength(key, text->size())) {
            return false;
        }
        outValue = PoolString::Intern(*text);
        return true;
    }

    bool CommandLineDeserializer::BeginObject(StringView name) {
        if (IsFlagKey(name)) {
            ReportError(name, kHoldsOneValue);
        }
        return false;
    }

    bool CommandLineDeserializer::BeginArray(StringView name, StringView /*elementName*/, size_t& count) {
        count = 0;
        if (IsFlagKey(name)) {
            ReportError(name, kHoldsOneValue);
        }
        return false;
    }

    bool CommandLineDeserializer::HasErrors() const {
        return !_errors.empty();
    }

    const Vector<DeserializeError>& CommandLineDeserializer::GetErrors() const {
        return _errors;
    }

    void CommandLineDeserializer::ClearErrors() {
        _errors.clear();
    }

    void CommandLineDeserializer::AddError(StringView path, StringView message, int32_t line) {
        const bool isRootKey = !path.empty() && !path.starts_with(kFlagPrefix) && path.find_first_of(".[") == StringView::npos;
        FlagBuffer buffer;
        const StringView flag = isRootKey ? MakeFlagName(path, buffer) : StringView{};
        StoreError(_errors, flag.empty() ? path : flag, message, line);
    }

}  // namespace BECore
