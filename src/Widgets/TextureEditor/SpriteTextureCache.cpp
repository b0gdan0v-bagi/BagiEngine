#include "SpriteTextureCache.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>

namespace BECore {

    const ITexture* SpriteTextureCache::Get(PoolString path) {
        if (path.Empty()) {
            return nullptr;
        }
        Slot& slot = _slots[path];
        if (slot.failed) {
            return nullptr;
        }
        if (!slot.handle || !CoreManager::GetResourceManager().GetCache().Holds(path, slot.handle.Get())) {
            Load(path, slot);
        }
        return slot.handle.Get();
    }

    void SpriteTextureCache::Load(PoolString path, Slot& slot) {
        slot.handle = CoreManager::GetResourceManager().Load<ITexture>(path.ToStringView());
        if (!slot.handle || slot.handle->GetState() != ResourceState::Loaded) {
            slot.handle.Reset();
            slot.failed = true;
        }
    }

    void SpriteTextureCache::ForgetFailed() {
        for (auto it = _slots.begin(); it != _slots.end();) {
            if (it->second.failed) {
                it = _slots.erase(it);
            } else {
                ++it;
            }
        }
    }

    void SpriteTextureCache::Clear() {
        _slots.clear();
    }

}  // namespace BECore
