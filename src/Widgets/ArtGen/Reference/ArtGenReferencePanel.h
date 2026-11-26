#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ArtGenSession;

    /// The tooltip of every Use as reference button: one text for the Generate tab and the Set tab
    inline constexpr const char* kArtGenUseAsReferenceTip = "Use this picture as the reference of the next single Generate (the Reference section of the Generate tab)";

    /**
     * @brief The picture of the session's reference slot, for the Generate panel's slot `artgenReferencePicture`; the path field, the mode, the
     * weight and Clear are the asset's (`GeneratePanel.xml`) over `ArtGenPanelModel`.
     *
     * ImGui only; the slot is the session's (`GetReferenceSlot`, `Reference/CLAUDE.md`), so a switch of the run mode that re-creates the
     * panel loses nothing. The thumbnail is loaded by path and evicted when the slot changes or the panel dies, like the variants'.
     */
    class ArtGenReferencePanel {
    public:
        ArtGenReferencePanel() = default;
        ArtGenReferencePanel(const ArtGenReferencePanel&) = delete;
        ArtGenReferencePanel& operator=(const ArtGenReferencePanel&) = delete;
        ~ArtGenReferencePanel();

        /// The slot's picture alone, its real path as the tooltip (the asset's slot `artgenReferencePicture`); an empty slot draws nothing and gives the
        /// picture back
        void DrawPicture(ArtGenSession& session);

        /// Evicts the loaded picture (the section is hidden, the slot is empty, the panel dies)
        void DropThumbnail();

    private:
        /// Loads the picture of the slot when it is not the loaded one; the strings are compared, nothing is made in a steady frame
        void SyncThumbnail(const String& gameRoot, const String& image);
        /// The loaded picture on its checkerboard, or an empty frame while it loads
        void DrawLoadedPicture();

        String _root;  // the game root and the slot's image the loaded picture was made from
        String _image;
        String _path;  // the real path that was loaded; empty when nothing is
        ResourceHandle<ITexture> _texture;
    };

}  // namespace BECore
