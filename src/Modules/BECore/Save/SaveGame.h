#pragma once
#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/FunctionRef.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace BECore {

    /// The virtual mount point of the save folder: a slot is read as `saves/<slot>.save`
    struct SaveMount {
        static constexpr auto point = "saves"_intern;
    };

    inline constexpr StringView kSaveMountPoint = SaveMount::point.View();

    inline constexpr StringView kSaveExtension = ".save";

    /// Added to the file name while WriteFileAtomically writes it; the file is renamed over the save when whole
    inline constexpr StringView kSaveTempExtension = kAtomicTempSuffix;

    inline constexpr size_t kMaxSlotNameLength = 64;

    /**
     * @brief Why a save game call failed
     *
     * NotInitialized, InvalidSlotName, NotFound, WriteFailed and ReadFailed come from the slot files. Corrupt, WrongGame,
     * VersionTooNew and VersionTooOld are about the contents and come from the envelope (Save / Load / Peek).
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
     * @brief Save slots: one file `<slot>.save` per slot in a folder that the service mounts as `saves`
     *
     * Not a CoreManager service: the caller passes the folder, so a test or a game decides where saves live. Writes go
     * through a temporary file renamed over the save, so a crash leaves the old save. The service owns the mount point
     * `saves`. Before Initialize() every call answers NotInitialized (a call that returns a plain value: false or empty).
     * Main thread only, like FileSystem. The contract in full: `Save/CLAUDE.md`.
     */
    class SaveGameService {
    public:
        /// @param fileSystem The file system the folder is mounted in; it has to outlive the service
        explicit SaveGameService(FileSystem& fileSystem);

        /**
         * @brief Create the save folder (with its parents) and mount it as `saves`
         *
         * A second call with another folder replaces the mount. A failed call changes nothing. A mount of kSaveMountPoint
         * made by anyone else is dropped.
         *
         * @return false if the path is a file or the folder cannot be made
         */
        bool Initialize(const std::filesystem::path& directory);

        [[nodiscard]] bool IsInitialized() const {
            return _initialized;
        }

        /// The folder of the saves, as mounted; empty before Initialize()
        [[nodiscard]] const std::filesystem::path& GetDirectory() const {
            return _directory;
        }

        /// 1 to kMaxSlotNameLength characters of [A-Za-z0-9_-]: no dot, slash or space, so a name cannot leave the folder
        [[nodiscard]] static bool IsValidSlotName(StringView slot);

        /**
         * @brief Write the bytes as the slot, replacing an existing save
         *
         * On a failure the old save stays and the temporary file is removed. Bytes over
         * ReaderLimits::kMaxBinaryStreamBytes are refused: ReadSlot would refuse the file.
         */
        [[nodiscard]] std::expected<void, SaveError> WriteSlot(StringView slot, const Vector<uint8_t>& bytes);

        /**
         * @brief Read the whole file of the slot
         *
         * @return NotFound when there is no such slot, ReadFailed when the file cannot be read or is over
         *         ReaderLimits::kMaxBinaryStreamBytes
         */
        [[nodiscard]] std::expected<Vector<uint8_t>, SaveError> ReadSlot(StringView slot) const;

        /**
         * @brief Write a game state as the slot: the envelope (game id, version) around the state
         *
         * @p writeState fills the object `state` of the file. It is not called when the slot name is bad or the service is
         * not initialized.
         */
        [[nodiscard]] std::expected<void, SaveError> Save(StringView slot, const SaveSchema& schema, FunctionRef<void(ISerializer&)> writeState);

        /**
         * @brief Read the slot and hand its `state` object to @p readState
         *
         * @p readState is called only when the envelope passes its checks (order: `Save/CLAUDE.md`, "The envelope"). An
         * error the deserializer records during it is Corrupt, and the state may then be partly filled: read into a
         * temporary object, not into the live one.
         *
         * @return what the file says; its version lets the caller migrate an older state
         */
        [[nodiscard]] std::expected<SaveInfo, SaveError> Load(StringView slot, const SaveSchema& schema, FunctionRef<void(IDeserializer&)> readState) const;

        /// Save() of a reflected state
        template <typename State>
        requires requires(const State& state, ISerializer& serializer) {
            state.Serialize(serializer);
        }
        [[nodiscard]] std::expected<void, SaveError> Save(StringView slot, const SaveSchema& schema, const State& state) {
            return Save(slot, schema, [&state](ISerializer& serializer) { state.Serialize(serializer); });
        }

        /**
         * @brief Load() into a reflected state
         *
         * @p state is untouched when the call fails before it is read (every error but Corrupt from the state itself).
         */
        template <typename State>
        requires requires(State& state, IDeserializer& deserializer) {
            state.Deserialize(deserializer);
        }
        [[nodiscard]] std::expected<SaveInfo, SaveError> Load(StringView slot, const SaveSchema& schema, State& state) const {
            return Load(slot, schema, [&state](IDeserializer& deserializer) { state.Deserialize(deserializer); });
        }

        /**
         * @brief Read only the envelope of the slot (game id and version), for a list of saves
         *
         * The state is not read and no schema is compared: the caller decides what the info means.
         */
        [[nodiscard]] std::expected<SaveInfo, SaveError> Peek(StringView slot) const;

        /// False for a bad name or before Initialize()
        [[nodiscard]] bool Exists(StringView slot) const;

        /// False when there is no such file, the name is bad or the service is not initialized
        bool Delete(StringView slot);

        /// Sorted by name; a leftover temporary file is not listed
        [[nodiscard]] Vector<PoolString> List() const;

    private:
        /// The real path of the save file of a valid slot, or an empty path when there is none in the save folder
        [[nodiscard]] std::filesystem::path ResolveSlot(StringView slot) const;

        /// The file need not exist
        [[nodiscard]] std::filesystem::path MakePath(StringView slot, StringView suffix) const;

        FileSystem& _fileSystem;
        std::filesystem::path _directory;
        bool _initialized = false;
    };

}  // namespace BECore
