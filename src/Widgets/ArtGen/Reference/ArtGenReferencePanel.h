#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ArtGenPresetForm;
    class ArtGenSession;

    /// The tooltip of every Use as reference button: one text for the Generate tab and the Set tab
    inline constexpr const char* kArtGenUseAsReferenceTip = "Use this picture as the reference of the next single Generate (the Reference section of the Generate tab)";

    /// The tooltip of Use as shape on an accepted sprite: the same slot as Use as reference, with the mode `structure`
    inline constexpr const char* kArtGenUseAsShapeTip = "Use this sprite's outline as the shape of the next single Generate (the Reference section of the Generate tab)";

    /**
     * @brief The Reference section of the Generate tab: a path field for a picture or sketch, the session's reference slot as a thumbnail,
     * its mode, its weight and Clear.
     *
     * ImGui only; the slot is the session's (`GetReferenceSlot`, `Reference/CLAUDE.md`), so a switch of the run mode that re-creates the
     * panel loses nothing. The thumbnail is loaded by path and evicted when the slot changes or the panel dies, like the variants'.
     */
    class ArtGenReferencePanel {
    public:
        /// The size of the path field's buffer
        static constexpr size_t kTypedPathSize = 512;

        ArtGenReferencePanel() = default;
        ArtGenReferencePanel(const ArtGenReferencePanel&) = delete;
        ArtGenReferencePanel& operator=(const ArtGenReferencePanel&) = delete;
        ~ArtGenReferencePanel();

        /// The section: the path field, then the picture, mode and weight of the slot with Clear, or the line that says the slot is empty
        void Draw(ArtGenSession& session);

        /// The "Reference" subsection of a preset's settings: edits the reference the preset file carries (mode, image path, weight), takes
        /// the slot into it or removes it. A static function: it keeps nothing, the form is the state
        static void DrawPresetReference(ArtGenSession& session, ArtGenPresetForm& form);

        /// The slot's picture alone, its real path as the tooltip (the asset's slot `artgenReferencePicture`); an empty slot draws nothing and gives the
        /// picture back
        void DrawPicture(ArtGenSession& session);

        /// Evicts the loaded picture (the section is hidden, the slot is empty, the panel dies)
        void DropThumbnail();

    private:
        /// The path field and Use file: fills the slot with the typed picture when the file exists (asked once, when the button is pressed)
        void DrawFileField(ArtGenSession& session);

        /// Loads the picture of the slot when it is not the loaded one; the strings are compared, nothing is made in a steady frame
        void SyncThumbnail(const String& gameRoot, const String& image);
        /// The loaded picture on its checkerboard, or an empty frame while it loads
        void DrawLoadedPicture();

        String _root;  // the game root and the slot's image the loaded picture was made from
        String _image;
        String _path;  // the real path that was loaded; empty when nothing is
        ResourceHandle<ITexture> _texture;
        char _typedPath[kTypedPathSize] = {};  // the path field's text
        bool _fileMissing = false;             // Use file found no such file; kept until the text changes
    };

}  // namespace BECore
