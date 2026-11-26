#include "FieldHelp.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <EASTL/algorithm.h>

namespace BECore {

    namespace {

        /// The config names of the two files, as `ConfigChangedEvent` gives them
        struct Ids {
            static constexpr auto engine = "FieldHelpConfig"_intern;
            static constexpr auto game = "GameFieldHelpConfig"_intern;
        };

        /// In the order of FieldHelpFile
        constexpr Array<StringView, 2> kFileNames = {"FieldHelpConfig.xml", "GameFieldHelpConfig.xml"};

        struct EntryKey {
            StringView component;
            StringView field;
        };

        EntryKey KeyOf(const FieldHelpEntry& entry) {
            return {entry.component.ToStringView(), entry.field.ToStringView()};
        }

        bool IsBefore(const EntryKey& left, const EntryKey& right) {
            return left.component != right.component ? left.component < right.component : left.field < right.field;
        }

        bool IsSame(const EntryKey& left, const EntryKey& right) {
            return left.component == right.component && left.field == right.field;
        }

        /// The place of @p key in the sorted @p entries: its entry, or where it would be inserted
        template <typename TEntries>
        auto PlaceOf(TEntries& entries, const EntryKey& key) {
            return eastl::lower_bound(entries.begin(), entries.end(), key, [](const FieldHelpEntry& entry, const EntryKey& wanted) { return IsBefore(KeyOf(entry), wanted); });
        }

        /// The file of the topmost `config` layer that has one; empty when none does. No lookup outside the layers: a file of the working directory is not content
        Path FindInLayers(const FileSystem& files, StringView fileName) {
            const Vector<Path> layers = files.GetMountLayers(MountPoints::config);
            for (size_t i = layers.size(); i > 0; --i) {
                Path candidate = layers[i - 1] / fileName;
                if (const Optional<FileInfo> info = GetFileInfo(candidate); info && info->kind == FileKind::File) {
                    return candidate;
                }
            }
            return {};
        }

        std::expected<FieldHelpConfig, String> ReadHelpFile(const Path& path) {
            XmlDeserializer reader;
            FieldHelpConfig config;
            if (reader.LoadFromFile(path)) {
                config.Deserialize(reader);
            }
            if (reader.HasErrors()) {
                const DeserializeError& first = reader.GetErrors().front();
                return std::unexpected<String>(Format("'{}': {} {}", path.View(), StringView(first.path), StringView(first.errorMessage)));
            }
            return config;
        }

    }  // namespace

    const FieldDesc* FindReflectedField(const TypeInfo& type, StringView name) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        for (const TypeInfo* owner = &type; owner != nullptr; owner = registry.Parent(*owner)) {
            for (const FieldDesc& field : owner->fields) {
                if (field.name == name) {
                    return &field;
                }
            }
        }
        return nullptr;
    }

    PoolString FieldHelp::ConfigName(FieldHelpFile file) {
        return file == FieldHelpFile::Game ? PoolString(Ids::game) : PoolString(Ids::engine);
    }

    bool FieldHelp::IsHelpConfig(PoolString name) {
        return name == Ids::engine || name == Ids::game;
    }

    const FileSystem& FieldHelp::Files() const {
        return _fileSystem != nullptr ? *_fileSystem : CoreManager::GetFileSystem();
    }

    Path FieldHelp::SourcePath(FieldHelpFile file) const {
        const StringView fileName = kFileNames[static_cast<size_t>(file)];
        if (Path found = FindInLayers(Files(), fileName); !found.Empty()) {
            return found;
        }
        const Vector<Path> layers = Files().GetMountLayers(MountPoints::config);
        if (layers.empty()) {
            return {};
        }
        return (file == FieldHelpFile::Game ? layers.back() : layers.front()) / fileName;
    }

    void FieldHelp::Reload() {
        for (const FieldHelpFile file : EnumUtils<FieldHelpFile>::Values()) {
            Table& table = TableOf(file);
            table = {};
            const Path path = FindInLayers(Files(), kFileNames[static_cast<size_t>(file)]);
            if (path.Empty()) {
                continue;
            }
            std::expected<FieldHelpConfig, String> read = ReadHelpFile(path);
            if (!read) {
                table.unreadable = true;
                LOG_WARNING("FieldHelp: {} (the file gives no descriptions and is not saved over)", read.error());
                continue;
            }
            table.entries.reserve(read->entries.size());
            for (FieldHelpEntry& entry : read->entries) {
                // Of two entries of one field the first of the file stays
                const EntryKey key = KeyOf(entry);
                const auto place = PlaceOf(table.entries, key);
                if (place == table.entries.end() || !IsSame(KeyOf(*place), key)) {
                    table.entries.insert(place, eastl::move(entry));
                }
            }
            table.stored = table.entries;
        }
        ++_version;
    }

    const FieldHelpEntry* FieldHelp::FindEntry(FieldHelpFile file, StringView component, StringView field) const {
        const Vector<FieldHelpEntry>& entries = _tables[static_cast<size_t>(file)].entries;
        const EntryKey key{component, field};
        const auto place = PlaceOf(entries, key);
        return place != entries.end() && IsSame(KeyOf(*place), key) ? &*place : nullptr;
    }

    FieldHelpAnswer FieldHelp::Find(StringView component, StringView field) const {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* const type = registry.Find(component);
        const TypeInfo* owner = type;
        StringView ownerName = component;
        while (true) {
            if (const Optional<FieldHelpFile> holder = HolderOf(ownerName, field)) {
                const FieldHelpEntry& entry = *FindEntry(*holder, ownerName, field);
                return {StringView(entry.text.data(), entry.text.size()), *holder == FieldHelpFile::Game ? FieldHelpSource::Game : FieldHelpSource::Engine};
            }
            owner = owner != nullptr ? registry.Parent(*owner) : nullptr;
            if (owner == nullptr) {
                break;
            }
            ownerName = owner->Name();
        }
        if (type != nullptr) {
            if (const FieldDesc* const desc = FindReflectedField(*type, field); desc != nullptr && !desc->attributes.tooltip.empty()) {
                return {desc->attributes.tooltip, FieldHelpSource::Attribute};
            }
        }
        return {};
    }

    Optional<FieldHelpFile> FieldHelp::HolderOf(StringView component, StringView field) const {
        for (const FieldHelpFile file : {FieldHelpFile::Game, FieldHelpFile::Engine}) {
            if (const FieldHelpEntry* const entry = FindEntry(file, component, field); entry != nullptr && !entry->text.empty()) {
                return file;
            }
        }
        return {};
    }

    bool FieldHelp::IsModified(FieldHelpFile file) const {
        const Table& table = TableOf(file);
        const auto same = [](const FieldHelpEntry& left, const FieldHelpEntry& right) { return IsSame(KeyOf(left), KeyOf(right)) && left.text == right.text; };
        return table.entries.size() != table.stored.size() || !eastl::equal(table.entries.begin(), table.entries.end(), table.stored.begin(), same);
    }

    void FieldHelp::Set(FieldHelpFile file, StringView component, StringView field, StringView text) {
        Vector<FieldHelpEntry>& entries = TableOf(file).entries;
        const EntryKey key{component, field};
        // A copy before the insert: @p text may view an entry of this very table
        String owned(text.data(), text.size());
        auto place = PlaceOf(entries, key);
        if (place == entries.end() || !IsSame(KeyOf(*place), key)) {
            FieldHelpEntry entry;
            entry.component = PoolString::Intern(component);
            entry.field = PoolString::Intern(field);
            place = entries.insert(place, eastl::move(entry));
        } else if (place->text == owned) {
            return;
        }
        place->text = eastl::move(owned);
        ++_version;
    }

    bool FieldHelp::Remove(FieldHelpFile file, StringView component, StringView field) {
        Vector<FieldHelpEntry>& entries = TableOf(file).entries;
        const EntryKey key{component, field};
        const auto place = PlaceOf(entries, key);
        if (place == entries.end() || !IsSame(KeyOf(*place), key)) {
            return false;
        }
        entries.erase(place);
        ++_version;
        return true;
    }

    std::expected<void, String> FieldHelp::Save(FieldHelpFile file) {
        const FileSystem& files = Files();
        const StringView fileName = kFileNames[static_cast<size_t>(file)];
        Table& table = TableOf(file);
        if (table.unreadable) {
            return std::unexpected<String>(Format("'{}' was not read and is not saved over", fileName));
        }
        const Path source = SourcePath(file);
        if (source.Empty()) {
            return std::unexpected<String>(Format("no 'config' folder to make '{}' in", fileName));
        }
        if (GetFileInfo(source)) {
            if (const std::expected<FieldHelpConfig, String> read = ReadHelpFile(source); !read) {
                return std::unexpected<String>(read.error());
            }
        }

        const Path target = files.WriteTarget(source);
        FieldHelpConfig config;
        config.entries = table.entries;
        // The serializer's document has its root element already
        XmlSerializer writer;
        config.Serialize(writer);
        if (!writer.SaveToFile(target)) {
            return std::unexpected<String>(Format("cannot write '{}'", target.View()));
        }
        table.stored = table.entries;
        return {};
    }

}  // namespace BECore
