#include "TextureLibrary.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>

namespace BECore {

    void TextureLibrary::Initialize() {
        _entries.clear();
        _index.clear();

        auto& configManager = CoreManager::GetConfigManager();
        if (!configManager.HasConfig("TextureLibrary"_intern)) {
            return;
        }

        auto xmlRoot = configManager.GetConfig("TextureLibrary"_intern);
        if (!xmlRoot) {
            return;
        }

        auto spritesNode = xmlRoot.GetChild("sprites");
        if (!spritesNode) {
            return;
        }

        for (const auto spriteNode : spritesNode.Children()) {
            if (spriteNode.Name() != eastl::string_view("sprite")) {
                continue;
            }

            SpriteEntry entry;

            auto nameAttr = spriteNode.GetAttribute("name");
            if (!nameAttr || nameAttr->empty()) {
                continue;
            }
            entry.name = PoolString::Intern(*nameAttr);

            auto pathAttr = spriteNode.GetAttribute("texturePath");
            if (pathAttr) {
                entry.texturePath = PoolString::Intern(*pathAttr);
            }

            auto srcNode = spriteNode.GetChild("srcRect");
            if (srcNode) {
                entry.srcRect.x = srcNode.ParseAttribute<float>("x").value_or(0.0f);
                entry.srcRect.y = srcNode.ParseAttribute<float>("y").value_or(0.0f);
                entry.srcRect.w = srcNode.ParseAttribute<float>("w").value_or(0.0f);
                entry.srcRect.h = srcNode.ParseAttribute<float>("h").value_or(0.0f);
            }

            AddOrUpdate(eastl::move(entry));
        }
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

        pugi::xml_node pugiRoot = xmlRoot.GetPugiNode();

        pugiRoot.remove_child("sprites");
        pugi::xml_node spritesEl = pugiRoot.append_child("sprites");

        for (const auto& entry : _entries) {
            pugi::xml_node spriteEl = spritesEl.append_child("sprite");
            spriteEl.append_attribute("name").set_value(entry.name.CStr());
            spriteEl.append_attribute("texturePath").set_value(entry.texturePath.CStr());

            pugi::xml_node srcRectEl = spriteEl.append_child("srcRect");
            srcRectEl.append_attribute("x").set_value(entry.srcRect.x);
            srcRectEl.append_attribute("y").set_value(entry.srcRect.y);
            srcRectEl.append_attribute("w").set_value(entry.srcRect.w);
            srcRectEl.append_attribute("h").set_value(entry.srcRect.h);
        }

        return configManager.SaveConfig("TextureLibrary"_intern);
    }

    void TextureLibrary::RebuildIndex() {
        _index.clear();
        for (size_t i = 0; i < _entries.size(); ++i) {
            _index[_entries[i].name] = i;
        }
    }

}  // namespace BECore
