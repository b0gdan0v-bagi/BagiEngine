#pragma once

#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioTypes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSound.h>
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
        void DrawPresetBar(ArtGenSession& session);
        void DrawPresetFields(ArtGenSession& session);
        void DrawSizeFields(const ArtGenWorkflow& workflow);
        void DrawInputField(const ArtGenWorkflowInput& input);
        void DrawResetButton(bool overridden, const char* id, FunctionRef<void()> reset);
        void SaveForm(ArtGenSession& session, StringView name);
        void DrawRun(ArtGenSession& session);
        void DrawVariants(const ArtGenRun& run);
        void DrawAccept(ArtGenSession& session);
        void SyncThumbnails(const ArtGenRun& run);
        void DropThumbnails();
        void DrawSounds(const ArtGenRun& run);
        void DrawSoundAccept(ArtGenSession& session);
        void DrawSoundSettings();
        void SyncSounds(const ArtGenRun& run);
        void DropSounds();
        void PlaySound(const AudioClip& clip, int32_t row);
        void StopSound();

        bool _open = false;

        // The preset being edited: loaded from the service's list, a new one, or a copy; Generate sends it inline while it is unsaved
        ArtGenPresetForm _form;
        char _saveAsName[ArtGenPresetForm::kNameSize] = {};
        String _formMessage;
        bool _formMessageIsError = false;
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

        /// One sound variant: its clip (loaded from the FLAC the service wrote), and once loaded its waveform and numbers
        struct SoundRow {
            String path;
            ResourceHandle<AudioClip> clip;
            Vector<float> waveform;
            ArtGenSoundStats stats;
            bool measured = false;
        };
        Vector<SoundRow> _sounds;
        AudioHandle _playing;
        int32_t _playingRow = -1;          // the row that plays; kPreviewRow: the processed preview
        IntrusivePtr<AudioClip> _preview;  // the selected sound as Accept would write it
        ArtGenSoundSettings _soundSettings;
        bool _replaceClip = false;  // Accept over a clip path of the game instead of assets/artgen/<name>.wav
        char _clipPath[256] = "assets/sounds/";
        String _previewError;

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
