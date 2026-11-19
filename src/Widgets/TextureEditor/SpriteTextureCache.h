#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceCache.h>
#include <BECore/Resource/ResourceHandle.h>

namespace BECore {

    /**
     * @brief Whether @p handle, kept for @p path, is a picture the cache has dropped: an Accept or a switch of the Generate panel (or Pack and
     * Unpack) rewrote the file and evicted the path, so the handle is the old picture and the path is to be loaded again.
     *
     * @return false for a null handle (nothing was loaded yet, which is not an evict)
     */
    [[nodiscard]] bool IsEvictedHandle(const ResourceCache& cache, PoolString path, const IResource* handle);

    /**
     * @brief Textures of the Sprite Library's list and atlas preview, one handle per virtual path.
     *
     * A texture is loaded once and kept; when the resource cache no longer holds it (Pack, Unpack and Accept evict the paths they
     * rewrite) the next `Get` loads the path again, so the picture is never stale. A failed load is remembered, so a file that is
     * gone logs once and not on every frame, until `ForgetFailed`.
     */
    class SpriteTextureCache {
    public:
        /// @return The loaded texture of @p path, or null (empty path, a failed load). Valid until the next `Get`, `ForgetFailed` or `Clear`.
        [[nodiscard]] const ITexture* Get(PoolString path);

        /// Lets the paths that failed be tried again: after a change that may have made a file appear, and on Rescan
        void ForgetFailed();

        void Clear();

    private:
        struct Slot {
            ResourceHandle<ITexture> handle;
            bool failed = false;
        };

        void Load(PoolString path, Slot& slot);

        UnorderedPoolMap<Slot> _slots;
    };

}  // namespace BECore
