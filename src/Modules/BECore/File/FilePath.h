#pragma once

#include <BECore/Types/Types.h>
#include <filesystem>  //-NoStd: the one place that turns a std::filesystem::path into the layer's UTF-8 StringView paths

namespace BECore {

    /**
     * The UTF-8 text of @p path, which is what every call of the file layer takes. `path::string()` is the ANSI code page on
     * Windows, so a path of the engine's code goes through here and nowhere else.
     */
    [[nodiscard]] inline String PathToUtf8(const std::filesystem::path& path) {
        const auto utf8 = path.u8string();  //-NoStd: char8_t text; its bytes are copied into a String at once
        return String(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }

}  // namespace BECore
