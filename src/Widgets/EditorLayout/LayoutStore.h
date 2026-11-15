#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>

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
     * Writes are atomic (UserFileIo.h). Main thread only.
     */
    class LayoutStore {
    public:
        /// @p userDirectory is the editor folder (EditorUserDirectory()); the store makes `layouts` inside it on the first Save
        explicit LayoutStore(std::filesystem::path userDirectory);

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
        [[nodiscard]] std::filesystem::path PathOf(StringView name) const;

        std::filesystem::path _folder;
    };

    /**
     * @brief The names of a LayoutStore kept between frames, so a menu that is open reads the folder once, not on every draw.
     *
     * Names() scans the store only when the list is stale (at the start, or after Invalidate()). Whoever changes the layouts
     * (save, delete) or opens a menu calls Invalidate(); a change made behind the cache's back shows at the next one.
     */
    class LayoutNameCache {
    public:
        /// The sorted names of @p store (LayoutStore::List()), scanned again only when stale. The reference holds until the next Names() call.
        [[nodiscard]] const Vector<String>& Names(const LayoutStore& store);

        /// Marks the list stale: the next Names() reads the folder
        void Invalidate() {
            _stale = true;
        }

    private:
        Vector<String> _names;
        bool _stale = true;
    };

}  // namespace BECore
