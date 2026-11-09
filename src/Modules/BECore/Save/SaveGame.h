#pragma once
#include <BECore/FileSystem/FileSystem.h>

namespace BECore {

    /// Name of the virtual mount point that the save folder is mounted as ("saves/<slot>.save")
    inline constexpr eastl::string_view kSaveMountPoint = "saves";

    /// Extension of a save file
    inline constexpr eastl::string_view kSaveExtension = ".save";

    /// Extension added to the name of a save file while it is being written (the file is renamed over the save when it is whole)
    inline constexpr eastl::string_view kSaveTempExtension = ".tmp";

    /// Longest slot name, in characters
    inline constexpr size_t kMaxSlotNameLength = 64;

    /**
     * @brief Why a save game call failed.
     *
     * NotInitialized: Initialize() was not called (or failed the first time). InvalidSlotName: the name is not a safe file name
     * (SaveGameService::IsValidSlotName). NotFound: no such slot. WriteFailed / ReadFailed: the disk refused. The last four are
     * for the file contents and are used by the layers on top of the plain slot files: Corrupt (not a save file, or
     * damaged), WrongGame (the file belongs to another game), VersionTooNew / VersionTooOld (the game's schema version is
     * outside what the file can be read as).
     */
    CORE_ENUM(SaveErrorKind, uint8_t, NotInitialized, InvalidSlotName, NotFound, WriteFailed, ReadFailed, Corrupt, WrongGame, VersionTooNew, VersionTooOld)

    /// A failed save game call: the kind to branch on and a sentence for the log (never empty)
    struct SaveError {
        SaveErrorKind kind = SaveErrorKind::NotInitialized;
        eastl::string detail;
    };

    /**
     * @brief Save slots: one file `<slot>.save` per slot in a folder that the service mounts as `saves`.
     *
     * A plain class, not yet a CoreManager service: the folder is passed in (Initialize), so a test or a game decides
     * where saves live. Reads go through the virtual path `saves/<slot>.save` of the FileSystem; writes land in the
     * mounted folder, as a temporary file that is renamed over the save, so a crash while writing leaves the old save.
     * Before Initialize() every call answers NotInitialized (the calls that return a plain value: false or empty).
     *
     * Not thread safe: call it from the main thread, like FileSystem.
     */
    class SaveGameService {
    public:
        /// @param fileSystem The file system the folder is mounted in; it has to outlive the service
        explicit SaveGameService(FileSystem& fileSystem);

        /**
         * @brief Create the save folder (with the folders above it) and mount it as `saves`
         *
         * A second call with another folder replaces the mount, so later reads come from the new folder. A failed call
         * changes nothing.
         *
         * @return false if the path is a file or the folder cannot be made
         */
        bool Initialize(const std::filesystem::path& directory);

        /// True after a successful Initialize()
        [[nodiscard]] bool IsInitialized() const {
            return _initialized;
        }

        /// The folder of the saves, as mounted; empty before Initialize()
        [[nodiscard]] const std::filesystem::path& GetDirectory() const {
            return _directory;
        }

        /// True for 1 to kMaxSlotNameLength characters of [A-Za-z0-9_-]: no dot, slash or space, so a name cannot leave the folder
        [[nodiscard]] static bool IsValidSlotName(eastl::string_view slot);

        /**
         * @brief Write the bytes as the slot, replacing an existing save
         *
         * The bytes go to `<slot>.save.tmp` first and are renamed over `<slot>.save`. On a failure the old save stays
         * and the temporary file is removed.
         *
         * @return InvalidSlotName, NotInitialized or WriteFailed on a failure
         */
        [[nodiscard]] std::expected<void, SaveError> WriteSlot(eastl::string_view slot, const eastl::vector<uint8_t>& bytes);

        /**
         * @brief Read the whole file of the slot
         *
         * @return InvalidSlotName, NotInitialized, NotFound (no such slot) or ReadFailed (it exists but cannot be read) on a failure
         */
        [[nodiscard]] std::expected<eastl::vector<uint8_t>, SaveError> ReadSlot(eastl::string_view slot) const;

        /// True if the slot has a save file (false for a bad name or before Initialize())
        [[nodiscard]] bool Exists(eastl::string_view slot) const;

        /// Remove the save file of the slot; false when there is none (or the name is bad, or before Initialize())
        bool Delete(eastl::string_view slot);

        /// Names of all slots that have a save file, sorted; a leftover temporary file is not listed
        [[nodiscard]] eastl::vector<PoolString> List() const;

    private:
        /// The real path of the save file of a valid slot, or an empty path when there is none in the save folder
        [[nodiscard]] std::filesystem::path ResolveSlot(eastl::string_view slot) const;

        /// The path of a file named `<slot><suffix>` in the save folder (the file need not exist)
        [[nodiscard]] std::filesystem::path MakePath(eastl::string_view slot, eastl::string_view suffix) const;

        FileSystem& _fileSystem;
        std::filesystem::path _directory;
        bool _initialized = false;
    };

}  // namespace BECore
