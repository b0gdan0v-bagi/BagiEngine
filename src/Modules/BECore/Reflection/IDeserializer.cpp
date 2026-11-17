#include <BECore/Reflection/IDeserializer.h>

namespace BECore {

    namespace {

        constexpr uint8_t kUtf8ContinuationMask = 0xC0;
        constexpr uint8_t kUtf8ContinuationBits = 0x80;

        constexpr StringView kTooManyErrors = "Too many errors: the rest are not recorded";

        constexpr uint8_t kFirstPrintable = 0x20;
        constexpr uint8_t kDelete = 0x7F;
        constexpr char kReplacementChar = '?';

        /// A message quotes the input (an enum name, a bad value) and reaches a log line and a reply: a control character in it is replaced
        String Printable(StringView text) {
            String printable(text.data(), text.size());
            for (char& c : printable) {
                const auto byte = static_cast<uint8_t>(c);
                if (byte < kFirstPrintable || byte == kDelete) {
                    c = kReplacementChar;
                }
            }
            return printable;
        }

        /// Names are input: both sides of an edit distance are cut to this many bytes, so the work of one suggestion is bounded
        constexpr size_t kMaxSuggestedNameBytes = 48;

        /// A suggestion is a name at most this many edits away, and the distance must also be under half the length of the unknown name
        constexpr size_t kMaxSuggestedDistance = 2;

        char AsciiLower(char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        /// Levenshtein distance of two names cut to kMaxSuggestedNameBytes, with the case of ASCII letters ignored (a case difference alone is no edit)
        size_t EditDistance(StringView a, StringView b) {
            a = a.substr(0, kMaxSuggestedNameBytes);
            b = b.substr(0, kMaxSuggestedNameBytes);
            Array<size_t, kMaxSuggestedNameBytes + 1> previous{};
            Array<size_t, kMaxSuggestedNameBytes + 1> current{};
            for (size_t j = 0; j <= b.size(); ++j) {
                previous[j] = j;
            }
            for (size_t i = 1; i <= a.size(); ++i) {
                current[0] = i;
                for (size_t j = 1; j <= b.size(); ++j) {
                    const size_t substitution = previous[j - 1] + (AsciiLower(a[i - 1]) == AsciiLower(b[j - 1]) ? 0 : 1);
                    current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
                }
                previous = current;
            }
            return previous[b.size()];
        }

    }  // namespace

    StringView IDeserializer::CutText(StringView text, size_t maxBytes) {
        if (text.size() <= maxBytes) {
            return text;
        }
        size_t length = maxBytes;
        while (length > 0 && (static_cast<uint8_t>(text[length]) & kUtf8ContinuationMask) == kUtf8ContinuationBits) {
            --length;
        }
        return text.substr(0, length);
    }

    void IDeserializer::StoreError(Vector<DeserializeError>& errors, StringView path, StringView message, int32_t line) {
        if (errors.size() >= ReaderLimits::kMaxRecordedErrors) {
            return;
        }

        DeserializeError error;
        if (errors.size() + 1 == ReaderLimits::kMaxRecordedErrors) {
            error.errorMessage = String(kTooManyErrors);
        } else {
            error.path = Printable(CutText(path, ReaderLimits::kMaxErrorTextBytes));
            error.errorMessage = Printable(CutText(message, ReaderLimits::kMaxErrorTextBytes));
            error.line = line;
        }
        errors.push_back(error);
    }

    bool IDeserializer::AcceptPoolStringLength(StringView key, size_t length) {
        if (length <= ReaderLimits::kMaxPoolStringBytes) {
            return true;
        }
        ReportError(key, Format("PoolString value is {} bytes, over the limit kMaxPoolStringBytes ({})", length, ReaderLimits::kMaxPoolStringBytes));
        return false;
    }

    String IDeserializer::MakeDidYouMeanHint(StringView unknown, std::initializer_list<const Vector<String>*> askedKeys) {
        const StringView cutName = CutText(unknown, kMaxSuggestedNameBytes);
        size_t bestDistance = kMaxSuggestedDistance + 1;
        StringView best;
        for (const Vector<String>* asked : askedKeys) {
            for (const String& candidate : *asked) {
                const size_t distance = EditDistance(cutName, candidate);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = StringView(candidate);
                }
            }
        }
        if (best.empty() || bestDistance * 2 >= cutName.size()) {
            return {};
        }
        return Format(", did you mean '{}'?", CutText(best, ReaderLimits::kMaxQuotedValueBytes));
    }

    void IDeserializer::RecordFinding(StringView key, StringView message) {
        const String fullPath = GetCurrentPath(key);
        if (_diagnostics == DiagnosticsMode::Warn) {
            StoreError(_warnings, StringView(fullPath), message, 0);
        } else {
            AddError(StringView(fullPath), message, 0);
        }
    }

}  // namespace BECore
