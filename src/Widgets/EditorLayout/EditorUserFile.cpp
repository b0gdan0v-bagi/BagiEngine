#include "EditorUserFile.h"

#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/File/File.h>

namespace BECore {

    namespace {

        constexpr int kJsonIndent = 2;
        constexpr StringView kBadFileSuffix = ".bad";
        // A UTF-8 byte order mark that Notepad writes: the tape refuses it, and a file the user edited by hand must not be moved to `.bad` for it
        constexpr StringView kUtf8Bom = "\xEF\xBB\xBF";

    }  // namespace

    bool ReadEditorJsonFile(const Path& path, String& text, JsonTape& tape, bool& unreadable) {
        unreadable = false;
        if (!GetFileInfo(path)) {
            return false;
        }
        unreadable = true;
        std::expected<String, String> read = ReadTextFile(path);
        if (!read) {
            LOG_WARNING("Editor settings: {}; using the defaults", read.error());
            return false;
        }
        text = std::move(*read);
        StringView body(text.data(), text.size());
        if (body.starts_with(kUtf8Bom)) {
            body.remove_prefix(kUtf8Bom.size());
        }
        if (!tape.Parse(body) || !JsonView(tape, 0).IsObject()) {
            LOG_WARNING("Editor settings: '{}' is not a JSON object; using the defaults", path.View());
            return false;
        }
        unreadable = false;
        return true;
    }

    std::expected<void, String> WriteEditorJsonFile(const Path& path, const JsonMemberMap& content, bool& unreadable) {
        // Moved aside once: the next write replaces a file the editor wrote itself
        if (unreadable) {
            const Path backup(Format("{}{}", path.View(), kBadFileSuffix));
            if (GetFileInfo(path) && RenameFile(path, backup)) {
                LOG_WARNING("Editor settings: the unreadable '{}' was kept as '{}'", path.View(), backup.View());
            }
            unreadable = false;
        }

        String text;
        JsonWriter writer(text, kJsonIndent);
        content.Write(writer);
        text.push_back('\n');
        return WriteFileAtomically(path, text);
    }

}  // namespace BECore
