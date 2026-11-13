#include "SaveGame.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Save/SaveStorage.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        bool IsSlotNameCharacter(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }

        std::unexpected<SaveError> MakeError(SaveErrorKind kind, eastl::string detail) {
            return std::unexpected(SaveError{kind, std::move(detail)});
        }

        std::unexpected<SaveError> NotInitializedError() {
            return MakeError(SaveErrorKind::NotInitialized, "The save folder is not set up: call SaveGameService::Initialize() first");
        }

        std::unexpected<SaveError> InvalidSlotNameError(eastl::string_view slot) {
            return MakeError(SaveErrorKind::InvalidSlotName, Format("'{}' is not a slot name: use 1 to {} characters of A-Z, a-z, 0-9, '_' and '-'", slot, kMaxSlotNameLength));
        }

        // The names of the entries of the envelope (see SaveGameService::Save)
        constexpr eastl::string_view kEnvelopeObject = "save";
        constexpr eastl::string_view kEnvelopeGame = "game";
        constexpr eastl::string_view kEnvelopeVersion = "version";
        constexpr eastl::string_view kEnvelopeState = "state";

        /// The first error the deserializer recorded as a sentence, or @p fallback when it recorded none
        eastl::string FirstErrorOr(const IDeserializer& deserializer, eastl::string_view fallback) {
            if (!deserializer.HasErrors()) {
                return eastl::string(fallback);
            }
            const DeserializeError& error = deserializer.GetErrors().front();
            return Format("{} (at '{}')", error.errorMessage.ToStringView(), error.path.ToStringView());
        }

        /// Open the bytes, check the shape of the envelope and read game and version; the deserializer stays inside `save`
        std::expected<SaveInfo, SaveError> OpenEnvelope(BinaryDeserializer& deserializer, const eastl::vector<uint8_t>& bytes, eastl::string_view slot) {
            if (!deserializer.LoadFromBuffer(bytes)) {
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

        std::string ToStdString(eastl::string_view text) {
            return std::string(text.data(), text.size());
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
            LOG_ERROR(Format("SaveGameService: cannot use '{}' as the save folder", absolute.string()).c_str());
            return false;
        }

        // A second call replaces the mount of the first, so reads never come from the old folder
        const PoolString mountPoint = PoolString::Intern(kSaveMountPoint);
        _fileSystem.Unmount(mountPoint);
        _initialized = _fileSystem.Mount(mountPoint, absolute);
        _directory = _initialized ? std::move(absolute) : std::filesystem::path{};
        return _initialized;
    }

    bool SaveGameService::IsValidSlotName(eastl::string_view slot) {
        return !slot.empty() && slot.size() <= kMaxSlotNameLength && eastl::all_of(slot.begin(), slot.end(), IsSlotNameCharacter);
    }

    std::filesystem::path SaveGameService::MakePath(eastl::string_view slot, eastl::string_view suffix) const {
        std::string name = ToStdString(slot);
        name.append(suffix.data(), suffix.size());
        return _directory / name;
    }

    std::filesystem::path SaveGameService::ResolveSlot(eastl::string_view slot) const {
        const eastl::string virtualPath = Format("{}/{}{}", kSaveMountPoint, slot, kSaveExtension);
        std::filesystem::path resolved = _fileSystem.ResolvePath(virtualPath);

        // ResolvePath falls back to the content roots and the working directory: a file of the same relative path there is not a save
        std::error_code ec;
        if (resolved.empty() || !std::filesystem::equivalent(resolved.parent_path(), _directory, ec)) {
            return {};
        }
        return resolved;
    }

    std::expected<void, SaveError> SaveGameService::WriteSlot(eastl::string_view slot, const eastl::vector<uint8_t>& bytes) {
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
        const std::filesystem::path temp = MakePath(slot, Format("{}{}", kSaveExtension, kSaveTempExtension));

        auto fail = [&](eastl::string detail) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return MakeError(SaveErrorKind::WriteFailed, std::move(detail));
        };

        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file) {
                return fail(Format("Cannot create '{}'", temp.string()));
            }
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            file.close();
            if (file.fail()) {
                return fail(Format("Cannot write '{}'", temp.string()));
            }
        }

        std::error_code ec;
        std::filesystem::rename(temp, target, ec);
        if (ec) {
            return fail(Format("Cannot replace '{}': {}", target.string(), ec.message()));
        }
        NotifySaveChanged();
        return {};
    }

    std::expected<eastl::vector<uint8_t>, SaveError> SaveGameService::ReadSlot(eastl::string_view slot) const {
        if (!_initialized) {
            return NotInitializedError();
        }
        if (!IsValidSlotName(slot)) {
            return InvalidSlotNameError(slot);
        }

        const std::filesystem::path path = ResolveSlot(slot);
        if (path.empty()) {
            return MakeError(SaveErrorKind::NotFound, Format("No save in slot '{}'", slot));
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            return MakeError(SaveErrorKind::ReadFailed, Format("'{}' is not a file", path.string()));
        }
        const uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec) {
            return MakeError(SaveErrorKind::ReadFailed, Format("Cannot get the size of '{}': {}", path.string(), ec.message()));
        }
        if (size > ReaderLimits::kMaxBinaryStreamBytes) {
            return MakeError(SaveErrorKind::ReadFailed, Format("'{}' is larger than the limit of {} bytes for a save", path.string(), ReaderLimits::kMaxBinaryStreamBytes));
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return MakeError(SaveErrorKind::ReadFailed, Format("Cannot open '{}'", path.string()));
        }

        eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (file.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return MakeError(SaveErrorKind::ReadFailed, Format("Cannot read '{}' to the end", path.string()));
        }
        return bytes;
    }

    std::expected<void, SaveError> SaveGameService::Save(eastl::string_view slot, const SaveSchema& schema, const eastl::function<void(ISerializer&)>& writeState) {
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

    std::expected<SaveInfo, SaveError> SaveGameService::Load(eastl::string_view slot, const SaveSchema& schema, const eastl::function<void(IDeserializer&)>& readState) const {
        BE_PROFILE_SCOPE("SaveGame::Load");
        const auto bytes = ReadSlot(slot);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }

        BinaryDeserializer deserializer;
        auto info = OpenEnvelope(deserializer, *bytes, slot);
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

    std::expected<SaveInfo, SaveError> SaveGameService::Peek(eastl::string_view slot) const {
        const auto bytes = ReadSlot(slot);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        BinaryDeserializer deserializer;
        return OpenEnvelope(deserializer, *bytes, slot);
    }

    bool SaveGameService::Exists(eastl::string_view slot) const {
        if (!_initialized || !IsValidSlotName(slot)) {
            return false;
        }
        std::error_code ec;
        const std::filesystem::path path = ResolveSlot(slot);
        return !path.empty() && std::filesystem::is_regular_file(path, ec);
    }

    bool SaveGameService::Delete(eastl::string_view slot) {
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

    eastl::vector<PoolString> SaveGameService::List() const {
        eastl::vector<PoolString> slots;
        if (!_initialized) {
            return slots;
        }

        const eastl::string_view extensions[] = {kSaveExtension};
        const eastl::string prefix = Format("{}/", kSaveMountPoint);
        for (const PoolString& file : _fileSystem.EnumerateFiles(PoolString::Intern(kSaveMountPoint), extensions)) {
            // "saves/<slot>.save": a file in a subfolder or with a name that is not a slot name is not a save of this service
            eastl::string_view name = file.ToStringView();
            if (!name.starts_with(prefix) || !name.ends_with(kSaveExtension)) {
                continue;
            }
            name.remove_prefix(prefix.size());
            name.remove_suffix(kSaveExtension.size());
            if (IsValidSlotName(name)) {
                slots.push_back(PoolString::Intern(name));
            }
        }

        eastl::sort(slots.begin(), slots.end(), [](const PoolString& a, const PoolString& b) { return a.ToStringView() < b.ToStringView(); });
        return slots;
    }

}  // namespace BECore
