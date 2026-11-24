#pragma once

#include <charconv>
#include <cmath>

namespace BECore::StrictText {

    // =========================================================================
    // The strict grammar of a text value: one place for every reader whose input is text (XmlDeserializer, CommandLineDeserializer),
    // so a number or a bool means the same in a config file and on the command line.
    // =========================================================================

    enum class Status { Ok, Malformed, OutOfRange };

    /// Spaces, tabs and line breaks at both ends are not part of the value
    StringView TrimAsciiSpaces(StringView text);

    /// A leading '+' is accepted (pugixml takes one) although std::from_chars does not; "+-1" and "++1" are malformed (the result is empty)
    StringView DropPlusSign(StringView text);

    /// `true`, `false`, `1` or `0` around optional spaces; anything else is Malformed and `outValue` is kept
    Status ParseBool(StringView text, bool& outValue);

    /// The whole text is a number of the type of `outValue` (not `nan` or `inf`); on a failure `outValue` is kept
    template <typename T>
    Status ParseNumber(StringView text, T& outValue) {
        text = DropPlusSign(TrimAsciiSpaces(text));
        if (text.empty()) {
            return Status::Malformed;
        }

        T parsed{};
        const char* const end = text.data() + text.size();
        const auto result = std::from_chars(text.data(), end, parsed);
        if (result.ec == std::errc::invalid_argument || result.ptr != end) {
            return Status::Malformed;
        }
        if (result.ec == std::errc::result_out_of_range) {
            return Status::OutOfRange;
        }
        if constexpr (std::is_floating_point_v<T>) {
            // "inf" and "nan" are text, not numbers of a config
            if (!std::isfinite(parsed)) {
                return Status::Malformed;
            }
        }
        outValue = parsed;
        return Status::Ok;
    }

    /// A bool through ParseBool, any other arithmetic type through ParseNumber
    template <typename T>
    Status Parse(StringView text, T& outValue) {
        if constexpr (std::is_same_v<T, bool>) {
            return ParseBool(text, outValue);
        } else {
            return ParseNumber(text, outValue);
        }
    }

    /// The word of the message for a target type: "boolean", "integer" or "number"
    template <typename T>
    constexpr const char* KindName() {
        if constexpr (std::is_same_v<T, bool>) {
            return "boolean";
        } else if constexpr (std::is_integral_v<T>) {
            return "integer";
        } else {
            return "number";
        }
    }

    /// `Invalid <kind> value '<shown>[...]'[ (out of range)]`. `shown` is the input already cut to ReaderLimits::kMaxQuotedValueBytes
    /// (IDeserializer::CutText), `wasCut` says that it was shorter than the input.
    String InvalidValueMessage(const char* kind, StringView shown, bool wasCut, Status status);

    /// The message for `text` read as a `T`; `shown` is `text` cut by the reader (IDeserializer::CutText)
    template <typename T>
    String InvalidValueMessage(StringView text, StringView shown, Status status) {
        return InvalidValueMessage(KindName<T>(), shown, shown.size() < text.size(), status);
    }

}  // namespace BECore::StrictText
