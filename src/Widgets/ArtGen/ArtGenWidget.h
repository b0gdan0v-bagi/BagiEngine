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
    struct ArtGenSetup;
    struct SceneSelectionChangedEvent;

    /**
     * @brief The editor's Generate panel: a style preset of the game and a subject become image variants through the art generation
     * service (tools/artgen), shown as a grid; a chosen one is accepted into the game as a sprite.
     *
     * Hidden at start; the command `view.generate` shows and hides it. The state lives in ArtGenSession, not here, so a switch of the run
     * mode that re-creates the panel loses nothing; the panel only draws it and turns clicks into requests. The thumbnails are loaded from
     * the files the service wrote and dropped from the resource cache when they leave the run (src/Widgets/ArtGen/CLAUDE.md).
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
        void OnSelectionChanged(const SceneSelectionChangedEvent& event);

        void DrawServiceLine(ArtGenSession& session);
        void DrawSetup(ArtGenSession& session);
        void DrawPlan(ArtGenSession& session, const ArtGenSetup& setup);
        void StartBridge(uint16_t port);
        void DrawForm(ArtGenSession& session);
        void DrawRun(ArtGenSession& session);
        void DrawVariants(const ArtGenRun& run);
        void DrawAccept(ArtGenSession& session);
        void SyncThumbnails(const ArtGenRun& run);
        void DropThumbnails();

        bool _open = false;

        String _presetName;
        char _subject[512] = {};
        int32_t _count = 4;
        bool _randomSeed = true;
        int64_t _seed = 0;
        bool _keepVariants = false;
        String _requestError;

        // One thumbnail per variant of the run, in its order; a path that leaves the run is evicted
        Vector<ResourceHandle<ITexture>> _thumbnails;
        Vector<String> _thumbnailPaths;
        Optional<int64_t> _selectedSeed;

        char _spriteName[65] = {};
        bool _useOnSelected = false;
        String _acceptMessage;
        bool _acceptFailed = false;

        // The bridge this panel started: when (ImGui time), so the line says "starting" until it polls; why it could not start
        double _bridgeStartedAt = -1.0;
        bool _bridgeWaitsForControl = false;  // Start the service turned the control API on: the bridge starts once it listens
        String _bridgeError;
        String _setupError;
    };

}  // namespace BECore
