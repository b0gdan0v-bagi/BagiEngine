#include "OpenDeserializer.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>

namespace BECore {

    namespace {

        constexpr StringView kXmlExtension = ".xml";
        constexpr StringView kBinaryExtension = ".bin";

        bool EndsWith(StringView text, StringView suffix) {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        /// The first error of the deserializer for a message, or a fallback when it recorded none.
        String FirstError(const IDeserializer& deserializer) {
            const auto& errors = deserializer.GetErrors();
            if (errors.empty()) {
                return "unknown error";
            }
            return String(errors.front().errorMessage.ToStringView());
        }

    }  // namespace

    std::expected<UniquePtr<IDeserializer>, String> OpenDeserializer(StringView virtualPath) {
        using Error = std::unexpected<String>;

        if (EndsWith(virtualPath, kXmlExtension)) {
            auto deserializer = eastl::make_unique<XmlDeserializer>();
            if (!deserializer->LoadFromVirtualPath(virtualPath)) {
                return Error(Format("Failed to load XML '{}': {}", virtualPath, FirstError(*deserializer)));
            }
            return UniquePtr<IDeserializer>(eastl::move(deserializer));
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
            return UniquePtr<IDeserializer>(eastl::move(deserializer));
        }

        return Error(Format("Unsupported file extension (expected .xml or .bin): '{}'", virtualPath));
    }

    void LogDeserializeErrors(StringView configName, const IDeserializer& deserializer) {
        const auto& errors = deserializer.GetErrors();
        LogDeserializeErrors(configName, Span<const DeserializeError>(errors.data(), errors.size()));
    }

    void LogDeserializeErrors(StringView configName, Span<const DeserializeError> errors) {
        for (const DeserializeError& error : errors) {
            // A document that fails to parse has no path, and the line is 0 where the format has none (binary)
            const String path = error.path.ToStringView().empty() ? String{} : Format("{}: ", error.path.ToStringView());
            const String line = error.line != 0 ? Format(" (line {})", error.line) : String{};
            LOG_ERROR(Format("Config '{}': {}{}{}", configName, path, error.errorMessage.ToStringView(), line).c_str());
        }
    }

}  // namespace BECore
