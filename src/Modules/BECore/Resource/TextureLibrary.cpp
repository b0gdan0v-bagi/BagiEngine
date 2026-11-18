#include "TextureLibrary.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Generated/TextureLibrary.gen.hpp>

namespace BECore {

    namespace {

        struct Ids {
            static constexpr auto config = "TextureLibrary"_intern;
            static constexpr const char* sprites = "sprites";
        };

    }  // namespace

    void TextureLibrary::Load(IDeserializer& deserializer) {
        Clear();

        size_t count = 0;
        if (!deserializer.BeginArray("sprites", "sprite", count)) {
            return;
        }
        for (size_t i = 0; i < count; ++i) {
            if (!deserializer.BeginArrayElement()) {
                break;
            }
            SpriteEntry entry;
            entry.Deserialize(deserializer);
            deserializer.EndArrayElement();
            if (!entry.name.Empty()) {
                AddOrUpdate(eastl::move(entry));
            }
        }
        deserializer.EndArray();
    }

    void TextureLibrary::Write(ISerializer& serializer) const {
        size_t count = _entries.size();
        if (!serializer.BeginArray("sprites", "sprite", count)) {
            return;
        }
        for (const auto& entry : _entries) {
            if (serializer.BeginArrayElement()) {
                entry.Serialize(serializer);
                serializer.EndArrayElement();
            }
        }
        serializer.EndArray();
    }

    void TextureLibrary::Initialize() {
        BE_PROFILE_SCOPE("TextureLibrary::Initialize");
        Clear();

        auto deserializer = CoreManager::GetConfigManager().CreateDeserializer(Ids::config);
        if (!deserializer) {
            return;
        }

        Load(*deserializer);
        deserializer->FinishLoad();
        LogDeserializeErrors("TextureLibrary", *deserializer);
    }

    const SpriteEntry* TextureLibrary::GetSprite(PoolString name) const {
        auto it = _index.find(name);
        if (it == _index.end()) {
            return nullptr;
        }
        return &_entries[it->second];
    }

    void TextureLibrary::AddOrUpdate(SpriteEntry entry) {
        auto it = _index.find(entry.name);
        if (it != _index.end()) {
            _entries[it->second] = eastl::move(entry);
        } else {
            _index[entry.name] = _entries.size();
            _entries.push_back(eastl::move(entry));
        }
    }

    void TextureLibrary::Remove(PoolString name) {
        auto it = _index.find(name);
        if (it == _index.end()) {
            return;
        }
        const size_t idx = it->second;
        _entries.erase(_entries.begin() + static_cast<ptrdiff_t>(idx));
        RebuildIndex();
    }

    bool TextureLibrary::Rename(PoolString from, PoolString to) {
        auto it = _index.find(from);
        if (it == _index.end() || to.Empty()) {
            return false;
        }
        if (from == to) {
            return true;
        }
        if (_index.find(to) != _index.end()) {
            return false;
        }
        _entries[it->second].name = to;
        RebuildIndex();
        return true;
    }

    PoolString TextureLibrary::MakeUniqueName(StringView base) const {
        PoolString candidate = PoolString::Intern(base);
        for (int suffix = 2; _index.find(candidate) != _index.end(); ++suffix) {
            candidate = PoolString::Intern(Format("{}_{}", base, suffix));
        }
        return candidate;
    }

    bool TextureLibrary::WriteInto(pugi::xml_node root) const {
        if (!root) {
            return false;
        }

        XmlSerializer serializer;
        Write(serializer);
        const pugi::xml_node written = serializer.GetRootNode().child(Ids::sprites);

        // Replaced where it stands: other children of the root may follow it
        const pugi::xml_node previous = root.child(Ids::sprites);
        if (previous) {
            root.insert_copy_before(written, previous);
            root.remove_child(previous);
        } else {
            root.append_copy(written);
        }
        return true;
    }

    bool TextureLibrary::Save() {
        auto& configManager = CoreManager::GetConfigManager();
        auto xmlRoot = configManager.GetConfig(Ids::config);
        if (!xmlRoot) {
            LOG_ERROR("TextureLibrary::Save: config not loaded");
            return false;
        }

        if (!WriteInto(xmlRoot.GetPugiNode())) {
            return false;
        }

        return configManager.SaveConfig(Ids::config);
    }

    void TextureLibrary::Clear() {
        _entries.clear();
        _index.clear();
    }

    void TextureLibrary::RebuildIndex() {
        _index.clear();
        for (size_t i = 0; i < _entries.size(); ++i) {
            _index[_entries[i].name] = i;
        }
    }

}  // namespace BECore
