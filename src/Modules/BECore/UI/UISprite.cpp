#include "UISprite.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/TextureLibrary.h>

namespace BECore {

    ITexture* UISprite::Resolve(PoolString name) {
        if (_direct || name == _loadedName) {
            return _texture.Get();
        }
        _loadedName = name;
        _texture.Reset();
        _source = {};
        if (name.Empty()) {
            return nullptr;
        }

        const SpriteEntry* entry = CoreManager::GetTextureLibrary().GetSprite(name);
        if (!entry) {
            LOG_ERROR("UISprite: sprite '{}' is not in the TextureLibrary"_format(name.ToStringView()));
            return nullptr;
        }
        const auto handle = CoreManager::GetResourceManager().Load<ITexture>(entry->texturePath.ToStringView());
        if (!handle) {
            LOG_ERROR("UISprite: cannot load the texture '{}' of sprite '{}'"_format(entry->texturePath.ToStringView(), name.ToStringView()));
            return nullptr;
        }
        _texture = handle.GetPtr();
        _source = entry->srcRect;
        return _texture.Get();
    }

    void UISprite::SetTexture(IntrusivePtr<ITexture> texture, const Rect& source) {
        _texture = std::move(texture);
        _source = source;
        _direct = static_cast<bool>(_texture);
    }

    Rect UISprite::FitInto(const Rect& area, const Rect& source) {
        if (source.IsEmpty()) {
            return area;
        }
        const float scale = std::min(area.w / source.w, area.h / source.h);
        Rect target;
        target.w = source.w * scale;
        target.h = source.h * scale;
        target.x = area.x + (area.w - target.w) * 0.5f;
        target.y = area.y + (area.h - target.h) * 0.5f;
        return target;
    }

    Rect UISprite::GetSource() const {
        if (!_source.IsEmpty() || !_texture) {
            return _source;
        }
        return {0.0f, 0.0f, _texture->GetWidth(), _texture->GetHeight()};
    }

}  // namespace BECore
