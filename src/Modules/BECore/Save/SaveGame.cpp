#include "SaveGame.h"

#include <BECore/File/File.h>
#include <BECore/File/FilePath.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Save/SaveStorage.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        bool IsSlotNameCharacter(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }

        std::unexpected<SaveError> MakeError(SaveErrorKind kind, String detail) {
            return std::unexpected(SaveError{kind, std::move(detail)});
        }

        std::unexpected<SaveError> NotInitializedError() {
            return MakeError(SaveErrorKind::NotInitialized, "The save folder is not set up: call SaveGameService::Initialize() first");
        }

        std::unexpected<SaveError> InvalidSlotNameError(StringView slot) {
            return MakeError(SaveErrorKind::InvalidSlotName, Format("'{}' is not a slot name: use 1 to {} characters of A-Z, a-z, 0-9, '_' and '-'", slot, kMaxSlotNameLength));
        }

        // The names of the entries of the envelope (see SaveGameService::Save)
        constexpr StringView kEnvelopeObject = "save";
        constexpr StringView kEnvelopeGame = "game";
        constexpr StringView kEnvelopeVersion = "version";
        constexpr StringView kEnvelopeState = "state";

        String FirstErrorOr(const IDeserializer& deserializer, StringView fallback) {
            if (!deserializer.HasErrors()) {
                return String(fallback);
            }
            const DeserializeError& error = deserializer.GetErrors().front();
            return Format("{} (at '{}')", error.errorMessage, error.path);
        }

        // A save is about 1 KB, so the bytes of a slot normally live in the call's scratch scope; the bound is the config's (XmlDocument), a bigger
        // slot is read to the heap so the thread's scratch arena does not keep its chunk
        constexpr size_t kMaxScratchSaveBytes = size_t{256} * 1024;

        /// The bytes of the slot file: in @p arena up to kMaxScratchSaveBytes (valid until the caller's ScratchScope ends), else on the heap
        std::expected<ScratchFileBytes, SaveError> ReadSlotScratch(const std::filesystem::path& path, LinearArena& arena) {
            auto bytes = ReadFileBytesScratch(PathToUtf8(path), arena, kMaxScratchSaveBytes, ReaderLimits::kMaxBinaryStreamBytes);
            if (!bytes) {
                return MakeError(SaveErrorKind::ReadFailed, std::move(bytes.error()));
            }
            return std::move(*bytes);
        }

        /// Leaves @p deserializer inside the object `save`, so Load goes on with `state`.
        /// The deserializer only views @p bytes: both callers declare their scratch scope and bytes before the deserializer, so they outlive it.
        std::expected<SaveInfo, SaveError> OpenEnvelope(BinaryDeserializer& deserializer, Span<const uint8_t> bytes, StringView slot) {
            if (!deserializer.LoadFromView(bytes.data(), bytes.size())) {
                return MakeError(SaveErrorKind::Corrupt, Format("The save in slot '{}' is not a valid file: {}", slot, FirstErrorOr(deserializer, "unreadable data")));
            }
            if (!deserializer.BeginObject(kEnvelopeObject)) {
                return MakeError(SaveErrorKind::Corrupt, Format("The file in slot '{}' is not a save: it has no '{}' object", slot, kEnvelopeObject));
            }

            SaveInfo info;
            if (!deserializer.ReadAttribute(kEnvelopeGame, info.game)) {
                return MakeError(SaveErrorKind::Corrupt, Format("The save in slot '{}' has no usable '{}': {}", slot, kEnvelopeGame, FirstErrorOr(deserializer, "the entry is missing")));
            }
            if (!deserializer.ReadAttribute(kEnvelopeVersion, info.version)) {
                return MakeError(SaveErrorKind::Corrupt, Format("The save in slot '{}' has no usable '{}': {}", slot, kEnvelopeVersion, FirstErrorOr(deserializer, "the entry is missing")));
            }
            return info;
        }

    }  // namespace

    SaveGameService::SaveGameService(FileSystem& fileSystem) : _fileSystem(fileSystem) {}

    bool SaveGameService::Initialize(const std::filesystem::path& directory) {
        if (directory.empty()) {
            return false;
        }

        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(directory, ec).lexically_normal();
        if (ec) {
            return false;
        }

        std::filesystem::create_directories(absolute, ec);
        if (ec || !std::filesystem::is_directory(absolute, ec)) {
            LOG_ERROR("SaveGameService: cannot use '{}' as the save folder", absolute.string());
            return false;
        }

        // A second call replaces the mount of the first, so reads never come from the old folder
        const PoolString mountPoint = SaveMount::point;
        _fileSystem.Unmount(mountPoint);
        _initialized = _fileSystem.Mount(mountPoint, absolute);
        _directory = _initialized ? std::move(absolute) : std::filesystem::path{};
        return _initialized;
    }

    bool SaveGameService::IsValidSlotName(StringView slot) {
        return !slot.empty() && slot.size() <= kMaxSlotNameLength && eastl::all_of(slot.begin(), slot.end(), IsSlotNameCharacter);
    }

    std::filesystem::path SaveGameService::MakePath(StringView slot, StringView suffix) const {
        //-NoStd: a std::filesystem path is built from it
        std::string name = ToStdString(slot);
        name.append(suffix.data(), suffix.size());
        return _directory / name;
    }

    std::filesystem::path SaveGameService::ResolveSlot(StringView slot) const {
        const String virtualPath = Format("{}/{}{}", kSaveMountPoint, slot, kSaveExtension);
        std::filesystem::path resolved = _fileSystem.ResolvePath(virtualPath);

        // ResolvePath falls back to the content roots and the working directory: a file of the same relative path there is not a save
        std::error_code ec;
        if (resolved.empty() || !std::filesystem::equivalent(resolved.parent_path(), _directory, ec)) {
            return {};
        }
        return resolved;
    }

    std::expected<void, SaveError> SaveGameService::WriteSlot(StringView slot, const Vector<uint8_t>& bytes) {
        if (!_initialized) {
            return NotInitializedError();
        }
        if (!IsValidSlotName(slot)) {
            return InvalidSlotNameError(slot);
        }

        if (bytes.size() > ReaderLimits::kMaxBinaryStreamBytes) {
            // A save the reader would refuse is no save: say so now and keep the old one
            return MakeError(SaveErrorKind::WriteFailed, Format("The save for slot '{}' is {} bytes, over the limit of {}", slot, bytes.size(), ReaderLimits::kMaxBinaryStreamBytes));
        }

        const std::filesystem::path target = MakePath(slot, kSaveExtension);
        auto written = WriteFileAtomically(PathToUtf8(target), Span<const uint8_t>(bytes.data(), bytes.size()));
        if (!written) {
            return MakeError(SaveErrorKind::WriteFailed, std::move(written.error()));
        }
        NotifySaveChanged();
        return {};
    }

    std::expected<std::filesystem::path, SaveError> SaveGameService::FindSlotFile(StringView slot) const {
        if (!_initialized) {
            return NotInitializedError();
        }
        if (!IsValidSlotName(slot)) {
            return InvalidSlotNameError(slot);
        }

        std::filesystem::path path = ResolveSlot(slot);
        if (path.empty()) {
            return MakeError(SaveErrorKind::NotFound, Format("No save in slot '{}'", slot));
        }
        return path;
    }

    std::expected<Vector<uint8_t>, SaveError> SaveGameService::ReadSlot(StringView slot) const {
        const auto path = FindSlotFile(slot);
        if (!path) {
            return std::unexpected(path.error());
        }

        auto bytes = ReadFileBytes(PathToUtf8(*path), ReaderLimits::kMaxBinaryStreamBytes);
        if (!bytes) {
            return MakeError(SaveErrorKind::ReadFailed, std::move(bytes.error()));
        }
        return std::move(*bytes);
    }

    std::expected<void, SaveError> SaveGameService::Save(StringView slot, const SaveSchema& schema, FunctionRef<void(ISerializer&)> writeState) {
        BE_PROFILE_SCOPE("SaveGame::Save");
        if (!_initialized) {
            return NotInitializedError();
        }
        if (!IsValidSlotName(slot)) {
            return InvalidSlotNameError(slot);
        }

        BinarySerializer serializer;
        serializer.BeginObject(kEnvelopeObject);
        serializer.WriteAttribute(kEnvelopeGame, schema.game);
        serializer.WriteAttribute(kEnvelopeVersion, schema.version);
        serializer.BeginObject(kEnvelopeState);
        writeState(serializer);
        serializer.EndObject();
        serializer.EndObject();
        return WriteSlot(slot, serializer.GetData());
    }

    std::expected<SaveInfo, SaveError> SaveGameService::Load(StringView slot, const SaveSchema& schema, FunctionRef<void(IDeserializer&)> readState) const {
        BE_PROFILE_SCOPE("SaveGame::Load");
        // The deserializer views the bytes and readState runs while they are alive: both end with this call. No co_await here
        ScratchScope scratch;
        const auto path = FindSlotFile(slot);
        if (!path) {
            return std::unexpected(path.error());
        }
        const auto bytes = ReadSlotScratch(*path, scratch.GetArena());
        if (!bytes) {
            return std::unexpected(bytes.error());
        }

        BinaryDeserializer deserializer;
        auto info = OpenEnvelope(deserializer, bytes->Bytes(), slot);
        if (!info) {
            return info;
        }
        if (info->game != schema.game) {
            return MakeError(SaveErrorKind::WrongGame, Format("The save in slot '{}' belongs to the game '{}', not '{}'", slot, info->game, schema.game));
        }
        if (info->version > schema.version) {
            return MakeError(SaveErrorKind::VersionTooNew, Format("The save in slot '{}' has version {}, this game reads up to {}", slot, info->version, schema.version));
        }
        if (info->version < schema.minVersion) {
            return MakeError(SaveErrorKind::VersionTooOld, Format("The save in slot '{}' has version {}, this game reads from {}", slot, info->version, schema.minVersion));
        }

        if (!deserializer.BeginObject(kEnvelopeState)) {
            return MakeError(SaveErrorKind::Corrupt, Format("The save in slot '{}' has no '{}' object", slot, kEnvelopeState));
        }
        readState(deserializer);
        deserializer.EndObject();
        deserializer.EndObject();
        if (deserializer.HasErrors()) {
            return MakeError(SaveErrorKind::Corrupt, Format("The state in slot '{}' cannot be read: {}", slot, FirstErrorOr(deserializer, "unknown error")));
        }
        return info;
    }

    std::expected<SaveInfo, SaveError> SaveGameService::Peek(StringView slot) const {
        ScratchScope scratch;
        const auto path = FindSlotFile(slot);
        if (!path) {
            return std::unexpected(path.error());
        }
        const auto bytes = ReadSlotScratch(*path, scratch.GetArena());
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        BinaryDeserializer deserializer;
        return OpenEnvelope(deserializer, bytes->Bytes(), slot);
    }

    bool SaveGameService::Exists(StringView slot) const {
        if (!_initialized || !IsValidSlotName(slot)) {
            return false;
        }
        std::error_code ec;
        const std::filesystem::path path = ResolveSlot(slot);
        return !path.empty() && std::filesystem::is_regular_file(path, ec);
    }

    bool SaveGameService::Delete(StringView slot) {
        if (!Exists(slot)) {
            return false;
        }
        std::error_code ec;
        const bool removed = std::filesystem::remove(ResolveSlot(slot), ec) && !ec;
        if (removed) {
            NotifySaveChanged();
        }
        return removed;
    }

    Vector<PoolString> SaveGameService::List() const {
        Vector<PoolString> slots;
        if (!_initialized) {
            return slots;
        }

        const StringView extensions[] = {kSaveExtension};
        const String prefix = Format("{}/", kSaveMountPoint);
        const Vector<PoolString> files = _fileSystem.EnumerateFiles(SaveMount::point, extensions);
        Vector<StringView> names;
        names.reserve(files.size());
        for (const PoolString& file : files) {
            // "saves/<slot>.save": a file in a subfolder or with a name that is not a slot name is not a save of this service
            StringView name = file.ToStringView();
            if (!name.starts_with(prefix) || !name.ends_with(kSaveExtension)) {
                continue;
            }
            name.remove_prefix(prefix.size());
            name.remove_suffix(kSaveExtension.size());
            if (IsValidSlotName(name)) {
                // A view into the text of the file's pool entry, which lives as long as the pool
                names.push_back(name);
            }
        }

        slots.resize(names.size());
        PoolString::InternBatch(Span<const StringView>(names.data(), names.size()), Span<PoolString>(slots.data(), slots.size()));
        eastl::sort(slots.begin(), slots.end(), [](const PoolString& a, const PoolString& b) { return a.ToStringView() < b.ToStringView(); });
        return slots;
    }

}  // namespace BECore
