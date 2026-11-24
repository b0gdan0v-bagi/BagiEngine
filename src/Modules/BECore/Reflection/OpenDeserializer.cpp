#include "OpenDeserializer.h"

#include <BECore/FileSystem/FileSystem.h>
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
            return String(errors.front().errorMessage);
        }

    }  // namespace

    std::expected<UniquePtr<IDeserializer>, String> OpenDeserializer(StringView virtualPath) {
        using Error = std::unexpected<String>;

        if (EndsWith(virtualPath, kXmlExtension)) {
            auto deserializer = MakeUnique<XmlDeserializer>();
            if (!deserializer->LoadFromVirtualPath(virtualPath)) {
                return Error(Format("Failed to load XML '{}': {}", virtualPath, FirstError(*deserializer)));
            }
            return UniquePtr<IDeserializer>(eastl::move(deserializer));
        }

        if (EndsWith(virtualPath, kBinaryExtension)) {
            const Path realPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
            if (realPath.Empty()) {
                return Error(Format("Failed to resolve virtual path '{}'", virtualPath));
            }
            auto deserializer = MakeUnique<BinaryDeserializer>();
            if (!deserializer->LoadFromFile(realPath)) {
                return Error(Format("Failed to load binary '{}': {}", virtualPath, FirstError(*deserializer)));
            }
            return UniquePtr<IDeserializer>(eastl::move(deserializer));
        }

        return Error(Format("Unsupported file extension (expected .xml or .bin): '{}'", virtualPath));
    }

    void LogDeserializeErrors(StringView configName, const IDeserializer& deserializer) {
        const auto& errors = deserializer.GetErrors();
        const auto& warnings = deserializer.GetWarnings();
        LogDeserializeErrors(configName, Span<const DeserializeError>(errors.data(), errors.size()), Span<const DeserializeError>(warnings.data(), warnings.size()));
    }

    void LogDeserializeErrors(StringView configName, Span<const DeserializeError> errors, Span<const DeserializeError> warnings) {
        // A document that fails to parse has no path, and the line is 0 where the format has none (binary)
        const auto text = [configName](const DeserializeError& message) {
            const String path = message.path.empty() ? String{} : Format("{}: ", message.path);
            const String line = message.line != 0 ? Format(" (line {})", message.line) : String{};
            return Format("Config '{}': {}{}{}", configName, path, message.errorMessage, line);
        };
        for (const DeserializeError& error : errors) {
            LOG_ERROR(text(error));
        }
        for (const DeserializeError& warning : warnings) {
            LOG_WARNING(text(warning));
        }
    }

}  // namespace BECore
