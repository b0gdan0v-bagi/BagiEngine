#pragma once

#include <BECore/Reflection/IDeserializer.h>

namespace BECore {

    /**
     * @brief Open a data file as an IDeserializer; the format is chosen by the file extension.
     *
     * `.xml` gives an XmlDeserializer and `.bin` a BinaryDeserializer (a file written by BinarySerializer::SaveToFile).
     * A caller does not construct a deserializer for a file itself; for a named config use ConfigManager::CreateDeserializer.
     *
     * @note This header does not include CoreManager.h (ResourceManager.h includes it), the definition does.
     *
     * @param virtualPath  Virtual path of the file (e.g., "config/SDLWindowConfig.xml").
     * @return The deserializer positioned at the root of the file, or a message when the extension is not supported,
     *         the path does not resolve or the file does not load.
     */
    std::expected<eastl::unique_ptr<IDeserializer>, eastl::string> OpenDeserializer(StringView virtualPath);

    /**
     * @brief Write every error the deserializer collected to the log, one LOG_ERROR per error.
     *
     * Form: `Config 'LoggerConfig': sinks[0].type: Unknown factory type: 'Console'`. The path is left out when the
     * error has none (a document that does not parse), and ` (line N)` is appended when it has a line.
     *
     * @param configName    Name of the config (or the path of the file) the deserializer read, for the message.
     * @param deserializer  Deserializer that finished (or failed) reading it.
     */
    void LogDeserializeErrors(StringView configName, const IDeserializer& deserializer);

    /// The same for errors that were copied out of a deserializer (a loaded settings struct keeps them), same message format.
    void LogDeserializeErrors(StringView configName, eastl::span<const DeserializeError> errors);

}  // namespace BECore
