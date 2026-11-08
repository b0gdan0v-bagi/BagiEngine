#include "OpenDeserializer.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view kXmlExtension = ".xml";
        constexpr eastl::string_view kBinaryExtension = ".bin";

        bool EndsWith(eastl::string_view text, eastl::string_view suffix) {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        /// The first error of the deserializer for a message, or a fallback when it recorded none.
        eastl::string FirstError(const IDeserializer& deserializer) {
            const auto& errors = deserializer.GetErrors();
            if (errors.empty()) {
                return "unknown error";
            }
            return eastl::string(errors.front().errorMessage.ToStringView());
        }

    }  // namespace

    std::expected<eastl::unique_ptr<IDeserializer>, eastl::string> OpenDeserializer(eastl::string_view virtualPath) {
        using Error = std::unexpected<eastl::string>;

        if (EndsWith(virtualPath, kXmlExtension)) {
            auto deserializer = eastl::make_unique<XmlDeserializer>();
            if (!deserializer->LoadFromVirtualPath(virtualPath)) {
                return Error(Format("Failed to load XML '{}': {}", virtualPath, FirstError(*deserializer)));
            }
            return eastl::unique_ptr<IDeserializer>(eastl::move(deserializer));
        }

        if (EndsWith(virtualPath, kBinaryExtension)) {
            const std::filesystem::path realPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
            if (realPath.empty()) {
                return Error(Format("Failed to resolve virtual path '{}'", virtualPath));
            }
            auto deserializer = eastl::make_unique<BinaryDeserializer>();
            if (!deserializer->LoadFromFile(realPath)) {
                return Error(Format("Failed to load binary '{}': {}", virtualPath, FirstError(*deserializer)));
            }
            return eastl::unique_ptr<IDeserializer>(eastl::move(deserializer));
        }

        return Error(Format("Unsupported file extension (expected .xml or .bin): '{}'", virtualPath));
    }

    void LogDeserializeErrors(eastl::string_view configName, const IDeserializer& deserializer) {
        const auto& errors = deserializer.GetErrors();
        LogDeserializeErrors(configName, eastl::span<const DeserializeError>(errors.data(), errors.size()));
    }

    void LogDeserializeErrors(eastl::string_view configName, eastl::span<const DeserializeError> errors) {
        for (const DeserializeError& error : errors) {
            // A document that fails to parse has no path, and the line is 0 where the format has none (binary)
            const eastl::string path = error.path.ToStringView().empty() ? eastl::string{} : Format("{}: ", error.path.ToStringView());
            const eastl::string line = error.line != 0 ? Format(" (line {})", error.line) : eastl::string{};
            LOG_ERROR(Format("Config '{}': {}{}{}", configName, path, error.errorMessage.ToStringView(), line).c_str());
        }
    }

}  // namespace BECore
