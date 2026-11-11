#pragma once

#include <BECore/Renderer/ITexture.h>

namespace BECore {

    /**
     * @brief The texture of a sprite that a UI element draws: found by the sprite's name in the TextureLibrary, or given directly by code.
     *
     * Not reflected: the element keeps the name in a reflected field and asks Resolve() with it on every draw, so a name edited in the inspector
     * is noticed. A texture given with SetTexture() (code, tests) wins over any name.
     */
    class UISprite {
    public:
        /**
         * @brief The texture to draw, or null when there is none.
         *
         * Loads the sprite of @p name on the first call and again whenever the name changes. A name the library does not know, or a texture that does not load,
         * is logged once and gives null until the name changes.
         */
        [[nodiscard]] ITexture* Resolve(PoolString name);

        /// Gives the texture directly. @p source is the part to draw; an empty one is the whole texture
        void SetTexture(IntrusivePtr<ITexture> texture, const Rect& source = {});

        /// The part of the texture to draw: the rect of the library entry or of SetTexture(), else the whole texture. Valid after Resolve()
        [[nodiscard]] Rect GetSource() const;

        /// Where a sprite whose part of the texture is @p source draws in @p area: the largest rect of the proportions of @p source that fits, centred, so a sprite is never squashed.
        /// @p area itself for an empty @p source (the whole texture, whatever its shape)
        [[nodiscard]] static Rect FitInto(const Rect& area, const Rect& source);

    private:
        IntrusivePtr<ITexture> _texture;
        Rect _source;
        PoolString _loadedName;
        bool _direct = false;
    };

}  // namespace BECore
