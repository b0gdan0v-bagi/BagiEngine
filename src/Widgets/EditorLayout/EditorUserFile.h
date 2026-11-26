#pragma once

#include <BECore/Control/JsonMemberMap.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /**
     * @brief Reads the per-user JSON file of the editor at @p path into @p text and tapes it (the tape borrows @p text, so both stay with the caller)
     *
     * False for a missing file, silently, and for a file that cannot be read, does not parse or is no object, with one warning and @p unreadable set.
     * A UTF-8 byte order mark at the start is skipped. Never writes.
     */
    [[nodiscard]] bool ReadEditorJsonFile(const Path& path, String& text, JsonTape& tape, bool& unreadable);

    /// Writes @p content atomically. A file the caller found @p unreadable is first moved to `<name>.bad` and the flag cleared, so the user's old text is not lost
    [[nodiscard]] std::expected<void, String> WriteEditorJsonFile(const Path& path, const JsonMemberMap& content, bool& unreadable);

}  // namespace BECore
