#pragma once
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>

namespace BECore {

    /// Name of the virtual mount point that the save folder is mounted as ("saves/<slot>.save")
    inline constexpr StringView kSaveMountPoint = "saves";

    /// Extension of a save file
    inline constexpr StringView kSaveExtension = ".save";

    /// Extension added to the name of a save file while it is being written (the file is renamed over the save when it is whole)
    inline constexpr StringView kSaveTempExtension = ".tmp";

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
        String detail;
    };

    /// What a game writes into a save and reads back: its id and the version of its state schema
    struct SaveSchema {
        /// The id of the game (a save of another game is refused as WrongGame)
        StringView game;
        /// The version of the state that is written now; a file with a higher version is refused as VersionTooNew
        uint32_t version = 1;
        /// The oldest version this game still reads; a file below it is refused as VersionTooOld
        uint32_t minVersion = 1;
    };

    /// What a save file says about itself, without its state (SaveGameService::Peek, and the result of Load)
    struct SaveInfo {
        String game;
        uint32_t version = 0;
    };

    /**
     * @brief Save slots: one file `<slot>.save` per slot in a folder that the service mounts as `saves`.
     *
     * A plain class, not yet a CoreManager service: the folder is passed in (Initialize), so a test or a game decides
     * where saves live. Reads go through the virtual path `saves/<slot>.save` of the FileSystem; writes land in the
     * mounted folder, as a temporary file that is renamed over the save, so a crash while writing leaves the old save.
     * The service owns the mount point `saves`: Initialize() replaces whatever was mounted under that name.
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
         * changes nothing. The service owns the mount point `saves` (kSaveMountPoint): a mount of that name made by anyone
         * else is dropped, so nothing else should mount it.
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
        [[nodiscard]] static bool IsValidSlotName(StringView slot);

        /**
         * @brief Write the bytes as the slot, replacing an existing save
         *
         * The bytes go to `<slot>.save.tmp` first and are renamed over `<slot>.save`. On a failure the old save stays
         * and the temporary file is removed. Bytes over ReaderLimits::kMaxBinaryStreamBytes are refused (ReadSlot would refuse the file).
         *
         * @return InvalidSlotName, NotInitialized or WriteFailed on a failure
         */
        [[nodiscard]] std::expected<void, SaveError> WriteSlot(StringView slot, const Vector<uint8_t>& bytes);

        /**
         * @brief Read the whole file of the slot
         *
         * @return InvalidSlotName, NotInitialized, NotFound (no such slot) or ReadFailed (it exists but cannot be read, or is over
         *         ReaderLimits::kMaxBinaryStreamBytes) on a failure
         */
        [[nodiscard]] std::expected<Vector<uint8_t>, SaveError> ReadSlot(StringView slot) const;

        /**
         * @brief Write a game state as the slot: the envelope (game id, version) around the state
         *
         * The file is one binary object `save` with `game` and `version` and a child object `state`, which @p writeState
         * fills. The file replaces an existing save as WriteSlot() does. @p writeState is not called when the slot name is
         * bad or the service is not initialized.
         *
         * @return the errors of WriteSlot()
         */
        [[nodiscard]] std::expected<void, SaveError> Save(StringView slot, const SaveSchema& schema, const eastl::function<void(ISerializer&)>& writeState);

        /**
         * @brief Read the slot and hand its `state` object to @p readState
         *
         * The checks come in this order, and @p readState is called only when all of them pass: the file is a valid
         * binary stream with an object `save` that has `game` and `version` (else Corrupt), `game` is the schema's (else
         * WrongGame), `version` is not above schema.version (VersionTooNew) and not below schema.minVersion
         * (VersionTooOld), the object `state` exists (else Corrupt). After @p readState any error the deserializer
         * recorded is Corrupt, and then the state may be partly filled: read into a temporary object, not into the live
         * one.
         *
         * @return what the file says (its version lets the caller migrate an older state), or ReadSlot()'s errors and the
         *         ones above
         */
        [[nodiscard]] std::expected<SaveInfo, SaveError> Load(StringView slot, const SaveSchema& schema, const eastl::function<void(IDeserializer&)>& readState) const;

        /// Save a reflected state (`state.Serialize(ISerializer&)`) as the slot; see the non-template Save()
        template <typename State>
        requires requires(const State& state, ISerializer& serializer) {
            state.Serialize(serializer);
        }
        [[nodiscard]] std::expected<void, SaveError> Save(StringView slot, const SaveSchema& schema, const State& state) {
            return Save(slot, schema, eastl::function<void(ISerializer&)>([&state](ISerializer& serializer) { state.Serialize(serializer); }));
        }

        /**
         * @brief Load a reflected state (`state.Deserialize(IDeserializer&)`) from the slot; see the non-template Load()
         *
         * @p state is untouched when the call fails before the state is read (every error but Corrupt from the state
         * itself); a Corrupt after that may leave it partly filled.
         */
        template <typename State>
        requires requires(State& state, IDeserializer& deserializer) {
            state.Deserialize(deserializer);
        }
        [[nodiscard]] std::expected<SaveInfo, SaveError> Load(StringView slot, const SaveSchema& schema, State& state) const {
            return Load(slot, schema, eastl::function<void(IDeserializer&)>([&state](IDeserializer& deserializer) { state.Deserialize(deserializer); }));
        }

        /**
         * @brief Read only the envelope of the slot (game id and version), for a list of saves
         *
         * The state is not read and not checked, and no schema is compared: the caller decides what the info means.
         *
         * @return ReadSlot()'s errors, or Corrupt when the file is not a save file
         */
        [[nodiscard]] std::expected<SaveInfo, SaveError> Peek(StringView slot) const;

        /// True if the slot has a save file (false for a bad name or before Initialize())
        [[nodiscard]] bool Exists(StringView slot) const;

        /// Remove the save file of the slot; false when there is none (or the name is bad, or before Initialize())
        bool Delete(StringView slot);

        /// Names of all slots that have a save file, sorted; a leftover temporary file is not listed
        [[nodiscard]] Vector<PoolString> List() const;

    private:
        /// The real path of the save file of a valid slot, or an empty path when there is none in the save folder
        [[nodiscard]] std::filesystem::path ResolveSlot(StringView slot) const;

        /// The path of a file named `<slot><suffix>` in the save folder (the file need not exist)
        [[nodiscard]] std::filesystem::path MakePath(StringView slot, StringView suffix) const;

        FileSystem& _fileSystem;
        std::filesystem::path _directory;
        bool _initialized = false;
    };

}  // namespace BECore
