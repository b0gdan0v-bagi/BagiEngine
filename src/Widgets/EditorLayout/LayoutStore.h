#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace BECore {

    /// A layout name is 1 to 64 characters
    inline constexpr size_t kMaxLayoutNameLength = 64;
    /// Folder of the layouts inside the editor folder, and the extension of a layout file
    inline constexpr StringView kLayoutsFolderName = "layouts";
    inline constexpr StringView kLayoutExtension = ".ini";

    CORE_ENUM(LayoutErrorKind, uint8_t, InvalidName, EmptyText, NotFound, ReadFailed, WriteFailed)

    struct LayoutError {
        LayoutErrorKind kind = LayoutErrorKind::ReadFailed;
        String detail;  // a sentence for the log or the window, never empty
    };

    /// True for 1 to 64 characters of [A-Za-z0-9_ -] with no leading or trailing space, that is not a Windows device
    /// name (CON, PRN, AUX, NUL, COM1..9, LPT1..9, in any case). No dot or slash: a name cannot leave the folder.
    [[nodiscard]] bool IsValidLayoutName(StringView name);

    /// True when @p text has the section of the dock tree (`[Docking][Data]`) that SaveIniSettingsToMemory writes. ImGui never
    /// reports a bad ini and clears the dock tree before it reads one, so the editor loads only a text that passes this.
    [[nodiscard]] bool LooksLikeLayoutText(StringView text);

    /**
     * @brief Named dock layouts, one file `<user folder>/layouts/<name>.ini` each.
     *
     * A file holds the text of ImGui::SaveIniSettingsToMemory, which ImGui::LoadIniSettingsFromMemory reads back. The
     * store only moves text, so it needs no ImGui context. Names are case-insensitive where the file system is
     * (Windows, a default macOS volume): `Wide` and `wide` are one layout there, and List() gives the case on disk.
     * Writes are atomic (`WriteFileAtomically`, `File/File.h`). Main thread only.
     */
    class LayoutStore {
    public:
        /// @p userDirectory is the editor folder (EditorUserDirectory()); the store makes `layouts` inside it on the first Save
        explicit LayoutStore(Path userDirectory);

        /// Writes @p text under @p name, replacing a layout of the same name. Refuses a bad name and an empty text.
        std::expected<void, LayoutError> Save(StringView name, StringView text);

        [[nodiscard]] std::expected<String, LayoutError> Load(StringView name) const;

        /// Removes the layout; NotFound when there is none
        std::expected<void, LayoutError> Delete(StringView name);

        [[nodiscard]] bool Exists(StringView name) const;

        /// The names of the saved layouts, sorted. Leftover `.tmp` files, other files, folders and files whose name
        /// is not a valid layout name are left out.
        [[nodiscard]] Vector<String> List() const;

    private:
        [[nodiscard]] Path PathOf(StringView name) const;

        Path _folder;
    };

    /**
     * @brief Tells whether a menu that is drawn only while open has just opened.
     *
     * A menu is drawn open on consecutive frames; it is not drawn at all while a parent menu is closed, so a flag cleared in the
     * "closed" branch can stay set. Opened() compares frame numbers instead: any gap since the last open draw means it was closed.
     */
    class MenuOpenTracker {
    public:
        /// Call on every frame the menu is drawn open, with the frame's number; true on the first such frame after a gap (or ever)
        [[nodiscard]] bool Opened(int frame) {
            const bool newlyOpened = !_seen || frame - _lastFrame != 1;
            _seen = true;
            _lastFrame = frame;
            return newlyOpened;
        }

    private:
        int _lastFrame = 0;
        bool _seen = false;
    };

}  // namespace BECore
