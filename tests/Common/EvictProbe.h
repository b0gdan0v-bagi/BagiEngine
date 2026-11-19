#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/IResourceLoader.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Common/RecordingRenderer.h>

namespace BECore::Tests {

    /**
     * @brief A texture file of its own kind that loads without a renderer: every load makes a new RecordedTexture, so a test sees whether
     * a holder took the cached one or loaded the path again after an Evict.
     */
    class EvictProbeLoader : public IResourceLoader {
    public:
        static constexpr StringView kExtension = ".evictprobe";
        static constexpr uint32_t kSide = 16;

        [[nodiscard]] bool CanLoad(StringView extension) const override {
            return extension == kExtension;
        }

        Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override {
            co_return LoadSync(path);
        }

        IntrusivePtr<IResource> LoadSync(PoolString) override {
            return New<RecordedTexture>(Vector<uint8_t>(static_cast<size_t>(kSide) * kSide * 4, 255), kSide, kSide);
        }
    };

    /**
     * @brief A sprite of the library whose texture comes from EvictProbeLoader; the loader is registered once per process.
     *
     * The destructor drops the sprite and the cached texture, so tests do not depend on each other.
     */
    class EvictProbeSprite {
    public:
        explicit EvictProbeSprite(StringView name = "EvictProbeSprite", const Rect& source = {0.0f, 0.0f, 8.0f, 8.0f})
            : _name(PoolString::Intern(name)), _path(PoolString::Intern(Format("evictprobe/{}.evictprobe", name))) {
            static const bool registered = [] {
                CoreManager::GetResourceManager().RegisterLoader(New<EvictProbeLoader>());
                return true;
            }();
            (void)registered;
            SetSource(source);
        }

        ~EvictProbeSprite() {
            CoreManager::GetTextureLibrary().Remove(_name);
            Evict();
        }

        EvictProbeSprite(const EvictProbeSprite&) = delete;
        EvictProbeSprite& operator=(const EvictProbeSprite&) = delete;

        /// Writes the library entry again with another part of the texture, as an Accept that changed the sprite does
        void SetSource(const Rect& source) const {
            SpriteEntry entry;
            entry.name = _name;
            entry.texturePath = _path;
            entry.srcRect = source;
            CoreManager::GetTextureLibrary().AddOrUpdate(entry);
        }

        /// What an Accept or a switch does to the path of the texture
        void Evict() const {
            CoreManager::GetResourceManager().Evict(_path.ToStringView());
        }

        /// The texture the cache holds now, to keep it alive while a test compares pointers: a freed texture's address may be reused by the next load
        [[nodiscard]] IntrusivePtr<IResource> Cached() const {
            return CoreManager::GetResourceManager().GetCache().Get(_path);
        }

        [[nodiscard]] PoolString Name() const {
            return _name;
        }

    private:
        PoolString _name;
        PoolString _path;
    };

}  // namespace BECore::Tests
