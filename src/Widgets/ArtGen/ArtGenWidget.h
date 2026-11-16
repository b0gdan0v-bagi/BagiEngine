#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <cstdint>

namespace BECore {

    class ArtGenSession;
    struct ArtGenRun;

    /**
     * @brief The editor's Generate panel: a style preset of the game and a subject become image variants through the art generation
     * service (tools/artgen), shown as a grid.
     *
     * Hidden at start; the command `view.generate` shows and hides it. The state lives in ArtGenSession, not here, so a switch of the run
     * mode that re-creates the panel loses nothing; the panel only draws it and turns clicks into requests. The thumbnails are loaded from
     * the files the service wrote and dropped from the resource cache when the run is replaced (src/Widgets/ArtGen/CLAUDE.md).
     */
    class ArtGenWidget : public IWidget {
        BE_CLASS(ArtGenWidget)
    public:
        ArtGenWidget() = default;
        ~ArtGenWidget() override;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void Draw() override;

    private:
        void OnCommand(const EditorCommandEvent& event);

        void DrawServiceLine(const ArtGenSession& session);
        void DrawForm(ArtGenSession& session);
        void DrawRun(ArtGenSession& session);
        void DrawVariants(const ArtGenRun& run);
        void SyncThumbnails(const ArtGenRun& run);
        void DropThumbnails();

        bool _open = false;

        String _presetName;
        char _subject[512] = {};
        int32_t _count = 4;
        bool _randomSeed = true;
        int64_t _seed = 0;
        String _requestError;

        uint32_t _thumbRunId = 0;
        Vector<ResourceHandle<ITexture>> _thumbnails;
        Vector<String> _thumbnailPaths;
        int32_t _selected = -1;
    };

}  // namespace BECore
