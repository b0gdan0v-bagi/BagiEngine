#include "SpriteRendererComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Generated/SpriteRendererComponent.gen.hpp>

namespace BECore {

    namespace {
        void ResolveFromLibrary(PoolString spriteId, PoolString& texturePath, Rect& srcRect) {
            if (spriteId.Empty()) {
                return;
            }
            const auto* entry = CoreManager::GetTextureLibrary().GetSprite(spriteId);
            if (!entry) {
                LOG_ERROR(Format("SpriteRendererComponent: sprite '{}' not found in TextureLibrary", spriteId.ToStringView()).c_str());
                return;
            }
            texturePath = entry->texturePath;
            srcRect = entry->srcRect;
        }
    }  // namespace

    void SpriteRendererComponent::OnAttached() {
        ResolveFromLibrary(_spriteId, _texturePath, _srcRect);

        if (_texturePath.Empty()) {
            return;
        }
        _texture = CoreManager::GetResourceManager().Load<ITexture>(_texturePath.ToStringView());
        if (!_texture) {
            LOG_ERROR(Format("SpriteRendererComponent: failed to load texture '{}'", _texturePath.ToStringView()).c_str());
        }
    }

    void SpriteRendererComponent::ReloadTexture() {
        _texture.Reset();

        ResolveFromLibrary(_spriteId, _texturePath, _srcRect);

        if (!_node || _texturePath.Empty()) {
            return;
        }

        _texture = CoreManager::GetResourceManager().Load<ITexture>(_texturePath.ToStringView());
        if (!_texture) {
            LOG_ERROR(Format("SpriteRendererComponent: failed to reload texture '{}'", _texturePath.ToStringView()).c_str());
        }
    }

    void SpriteRendererComponent::SetTexture(IntrusivePtr<ITexture> texture) {
        _texture = ResourceHandle<ITexture>(std::move(texture));
    }

}  // namespace BECore
