#pragma once

#include <BECore/File/File.h>
#include <BECore/File/FilePath.h>
#include <BECore/Format/Format.h>
#include <BECore/Types/Types.h>
#include <filesystem>

namespace BECore::Tests {

    /// The text of config/@p name of the engine, read from the source tree; empty when there is none
    inline String ReadShippedConfig(StringView name) {
        const std::filesystem::path file = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "config" / std::string(name.data(), name.size());  //-NoStd: a path
        auto text = ReadTextFile(PathToUtf8(file));
        return text ? std::move(*text) : String();
    }

    /**
     * @brief Turns every `<@p tag type="...">` of @p xml into `<@p tag type="@p newType" id="k">`, k = 1, 2, ... in file order.
     *
     * A test runs a shipped list of sinks or handlers through a probe type that records its id, so it reads the order the entries are served in without
     * the side effects of the real ones (a file, a debug break). Entries with `enabled="false"` keep their number, so the ids stay the file positions.
     */
    inline String NumberEntries(const String& xml, StringView tag, StringView newType) {
        const String opening = Format("<{} type=\"", tag);
        String result;
        size_t from = 0;
        int32_t number = 0;
        for (size_t pos = xml.find(opening); pos != String::npos; pos = xml.find(opening, from)) {
            const size_t typeEnd = xml.find('"', pos + opening.size());
            if (typeEnd == String::npos) {
                break;
            }
            result.append(xml, from, pos + opening.size() - from);
            result += Format("{}\" id=\"{}", newType, ++number);
            from = typeEnd;
        }
        result.append(xml, from, String::npos);
        return result;
    }

}  // namespace BECore::Tests
