#include "TextureLibrary.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Generated/TextureLibrary.gen.hpp>

namespace BECore {

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
        Clear();

        auto deserializer = CoreManager::GetConfigManager().CreateDeserializer("TextureLibrary"_intern);
        if (!deserializer) {
            return;
        }

        Load(*deserializer);
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

    bool TextureLibrary::Save() {
        auto& configManager = CoreManager::GetConfigManager();
        auto xmlRoot = configManager.GetConfig("TextureLibrary"_intern);
        if (!xmlRoot) {
            LOG_ERROR("TextureLibrary::Save: config not loaded");
            return false;
        }

        XmlSerializer serializer;
        Write(serializer);

        // The one place that still touches pugi: replace the `sprites` section of the cached document and keep the rest of the file
        // (other children, comments). A ConfigManager helper for "replace a section of a config" is for the editors' slice.
        pugi::xml_node pugiRoot = xmlRoot.GetPugiNode();
        pugiRoot.remove_child("sprites");
        pugiRoot.append_copy(serializer.GetRootNode().child("sprites"));

        return configManager.SaveConfig("TextureLibrary"_intern);
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
