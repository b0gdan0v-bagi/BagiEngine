#pragma once

#include <BECore/Widgets/IWidget.h>
#include <EASTL/vector.h>

namespace BECore {

    /**
     * @brief ImGui widget for managing the TextureLibrary sprite definitions
     *
     * Two-panel layout:
     *   Left  - Scrollable list of all named sprite entries; [+ Add] button
     *   Right - Edit the selected entry (name, texture file, srcRect); [Delete] button
     *
     * [Save] persists changes to config/TextureLibrary.xml.
     */
    class TextureEditorWidget : public IWidget {
        BE_CLASS(TextureEditorWidget)
    public:
        TextureEditorWidget() = default;
        ~TextureEditorWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        void RenderLeftPanel();
        void RenderRightPanel();
        void RefreshAssetList();

        int _selectedIndex = -1;
        bool _isDirty = false;

        eastl::vector<PoolString> _cachedAssets;
        bool _assetsLoaded = false;

        char _nameEditBuffer[256] = {};
        bool _nameDirty = false;
    };

}  // namespace BECore
