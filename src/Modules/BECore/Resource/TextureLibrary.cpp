#include "TextureLibrary.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/MemTag.h>
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
        // `count` is an upper bound (a nameless entry is skipped, a repeated name overwrites): the capacity is never short
        _entries.reserve(_entries.size() + count);
        ReserveIndex(_entries.size() + count);
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
        BE_MEM_TAG(Resource);
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
        if (_slots.empty()) {
            return nullptr;
        }
        const uint32_t value = _slots[FindSlot(name)];
        return value == kFreeSlot ? nullptr : &_entries[value - 1];
    }

    void TextureLibrary::AddOrUpdate(SpriteEntry entry) {
        ReserveIndex(_entries.size() + 1);
        const size_t slot = FindSlot(entry.name);
        if (_slots[slot] != kFreeSlot) {
            _entries[_slots[slot] - 1] = eastl::move(entry);
        } else {
            _entries.push_back(eastl::move(entry));
            _slots[slot] = static_cast<uint32_t>(_entries.size());
        }
    }

    void TextureLibrary::Remove(PoolString name) {
        const SpriteEntry* entry = GetSprite(name);
        if (entry == nullptr) {
            return;
        }
        _entries.erase(_entries.begin() + (entry - _entries.data()));
        RebuildIndex(_entries.size());
    }

    bool TextureLibrary::Rename(PoolString from, PoolString to) {
        const SpriteEntry* entry = GetSprite(from);
        if (entry == nullptr || to.Empty()) {
            return false;
        }
        if (from == to) {
            return true;
        }
        if (GetSprite(to) != nullptr) {
            return false;
        }
        _entries[static_cast<size_t>(entry - _entries.data())].name = to;
        RebuildIndex(_entries.size());
        return true;
    }

    PoolString TextureLibrary::MakeUniqueName(StringView base) const {
        PoolString candidate = PoolString::Intern(base);
        for (int suffix = 2; GetSprite(candidate) != nullptr; ++suffix) {
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
        _slots.clear();
    }

    size_t TextureLibrary::FindSlot(PoolString name) const {
        const size_t mask = _slots.size() - 1;
        // Linear probing ends: the table is never more than half full, so a free slot is always ahead
        for (size_t slot = static_cast<size_t>(name.HashValue()) & mask;; slot = (slot + 1) & mask) {
            const uint32_t value = _slots[slot];
            if (value == kFreeSlot || _entries[value - 1].name == name) {
                return slot;
            }
        }
    }

    void TextureLibrary::ReserveIndex(size_t count) {
        if (_slots.size() < kMinSlots || _slots.size() < count * kSlotsPerEntry) {
            RebuildIndex(count);
        }
    }

    void TextureLibrary::RebuildIndex(size_t count) {
        size_t size = eastl::max(kMinSlots, _slots.size());
        while (size < count * kSlotsPerEntry) {
            size *= 2;
        }
        // The size is a power of two, so FindSlot masks instead of dividing; assign keeps the block when the size fits
        _slots.assign(size, kFreeSlot);
        for (size_t i = 0; i < _entries.size(); ++i) {
            _slots[FindSlot(_entries[i].name)] = static_cast<uint32_t>(i + 1);
        }
    }

}  // namespace BECore
