#include <BECore/Reflection/IDeserializer.h>

namespace BECore {

    namespace {

        constexpr uint8_t kUtf8ContinuationMask = 0xC0;
        constexpr uint8_t kUtf8ContinuationBits = 0x80;

        constexpr eastl::string_view kTooManyErrors = "Too many errors: the rest are not recorded";

    }  // namespace

    eastl::string_view IDeserializer::CutText(eastl::string_view text, size_t maxBytes) {
        if (text.size() <= maxBytes) {
            return text;
        }
        size_t length = maxBytes;
        while (length > 0 && (static_cast<uint8_t>(text[length]) & kUtf8ContinuationMask) == kUtf8ContinuationBits) {
            --length;
        }
        return text.substr(0, length);
    }

    void IDeserializer::StoreError(eastl::vector<DeserializeError>& errors, eastl::string_view path, eastl::string_view message, int32_t line) {
        if (errors.size() >= ReaderLimits::kMaxRecordedErrors) {
            return;
        }

        DeserializeError error;
        if (errors.size() + 1 == ReaderLimits::kMaxRecordedErrors) {
            error.errorMessage = PoolString::Intern(kTooManyErrors);
        } else {
            error.path = PoolString::Intern(CutText(path, ReaderLimits::kMaxErrorTextBytes));
            error.errorMessage = PoolString::Intern(CutText(message, ReaderLimits::kMaxErrorTextBytes));
            error.line = line;
        }
        errors.push_back(error);
    }

}  // namespace BECore
