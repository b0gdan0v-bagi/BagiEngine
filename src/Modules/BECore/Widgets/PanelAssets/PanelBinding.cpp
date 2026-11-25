#include "PanelBinding.h"

#include <BECore/Reflection/PropertyPath.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLimits.h>

namespace BECore {

    namespace {

        constexpr char kNegation = '!';
        constexpr char kSourceSeparator = '.';

        bool IsSourceStart(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        bool IsSourceChar(char c) {
            return IsSourceStart(c) || (c >= '0' && c <= '9');
        }

        std::expected<PanelBinding, String> Parse(StringView text, bool allowNegation, bool allowSourceOnly) {
            if (text.size() > PanelAssetLimits::kMaxTextBytes) {
                return std::unexpected(String("the binding is too long"));
            }

            PanelBinding binding;
            if (!text.empty() && text.front() == kNegation) {
                if (!allowNegation) {
                    return std::unexpected(String("'!' is allowed in a condition only"));
                }
                binding.negated = true;
                text.remove_prefix(1);
            }

            const size_t separator = text.find(kSourceSeparator);
            if (separator == StringView::npos) {
                if (!allowSourceOnly) {
                    return std::unexpected(String("expected 'source.path'"));
                }
                if (!IsValidSourceName(text)) {
                    return std::unexpected(String("the source name is empty or has a character other than letters, digits and '_'"));
                }
                binding.source = String(text);
                return binding;
            }
            const StringView source = text.substr(0, separator);
            if (!IsValidSourceName(source)) {
                return std::unexpected(String("the source name is empty or has a character other than letters, digits and '_'"));
            }

            const StringView path = text.substr(separator + 1);
            if (path.empty()) {
                return std::unexpected(String("the path after the source is empty"));
            }
            const auto parsed = ParsePropertyPath(path);
            if (!parsed) {
                return std::unexpected(Format("bad path: {}", parsed.error().message));
            }

            binding.source = String(source);
            binding.path = String(path);
            return binding;
        }

    }  // namespace

    bool IsValidSourceName(StringView name) {
        if (name.empty() || !IsSourceStart(name.front())) {
            return false;
        }
        for (const char c : name) {
            if (!IsSourceChar(c)) {
                return false;
            }
        }
        return true;
    }

    std::expected<PanelBinding, String> ParseBinding(StringView text) {
        return Parse(text, false, false);
    }

    std::expected<PanelBinding, String> ParseObjectBinding(StringView text) {
        return Parse(text, false, true);
    }

    std::expected<PanelBinding, String> ParseCondition(StringView text) {
        return Parse(text, true, false);
    }

}  // namespace BECore
