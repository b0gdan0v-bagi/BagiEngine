#pragma once

#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioTypes.h>
#include <BECore/Control/ControlTypes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/ArtGen/ArtGenAnswers.h>
#include <Widgets/ArtGen/ArtGenImage.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSound.h>
#include <Widgets/ArtGen/ArtGenVerdict.h>
#include <Widgets/ArtGen/Reference/ArtGenReferencePanel.h>
#include <Widgets/ArtGen/Sets/ArtGenSetPanel.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <cstdint>
#include <expected>

namespace BECore {

    class ArtGenSession;
    struct ArtGenRun;
    struct ArtGenSetup;
    struct ArtGenVariant;
    struct SceneSelectionChangedEvent;
    struct ArtGenVariantDropEvent;

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
        /// A panel dropped a dragged variant (ArtGenDrag.h): accepts it as the sprite the event names and answers in the event's result
        void OnVariantDropped(const ArtGenVariantDropEvent& event);
        /// Takes the answer of an image Accept (the button's or a drop's) into the message line and the sprite Use as reference takes
        void NoteImageAccepted(StringView sprite, const std::expected<ArtGenAcceptAnswer, ControlError>& result);

        void DrawServiceLine(ArtGenSession& session);
        void DrawSetup(ArtGenSession& session);
        void DrawPlan(ArtGenSession& session, const ArtGenSetup& setup);
        void StartBridge(uint16_t port);
        /// The preset section above the tabs: what Generate and Set both use
        void DrawPreset(ArtGenSession& session);
        /// Subject, count, seed and Generate: the Generate tab's own controls
        void DrawRunForm(ArtGenSession& session);
        /// The reference slot goes with an image preset only: a sound has none
        [[nodiscard]] bool PresetMakesImages(const ArtGenSession& session) const;
        void DrawPresetBar(ArtGenSession& session);
        void DrawPresetFields(ArtGenSession& session);
        void DrawSizeFields(const ArtGenWorkflow& workflow);
        void DrawInputField(const ArtGenWorkflowInput& input);
        void DrawResetButton(bool overridden, const char* id, FunctionRef<void()> reset);
        void SaveForm(ArtGenSession& session, StringView name);
        void DrawRun(ArtGenSession& session);
        void DrawVariants(ArtGenSession& session, const ArtGenRun& run);
        void DrawAccept(ArtGenSession& session);
        /// Trim, padding and max size of the form's `accept`: one drawing for the preset's Settings and the Accept section (@p id keeps the two apart)
        void DrawImageAcceptFields(const char* id);
        /// Reads the selected image's drawn box when the selection changes and makes the summary line again when that or a setting changed
        void RefreshAcceptSummary(const ArtGenRun& run, const ArtGenVariant& variant);
        void SyncThumbnails(const ArtGenRun& run);
        void DropThumbnails();
        void DrawSounds(const ArtGenRun& run);
        void DrawSoundAccept(ArtGenSession& session);
        void DrawSoundSettings();
        void SyncSounds(const ArtGenRun& run);
        void DropSounds();
        void PlaySound(const AudioClip& clip, int32_t row);
        void StopSound();
        /// Forgets a sound that ended by itself
        void RefreshPlaying();
        /// The Set tab is not on screen: stops its sound and gives its pictures back
        void ReleaseSetTab();

        bool _open = false;
        ArtGenSetPanel _setPanel;
        ArtGenReferencePanel _referencePanel;

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
        Vector<ArtGenVerdict> _verdicts;
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
        String _acceptedSprite;  // the sprite the last successful Accept wrote: Use as reference takes it

        // What Accept would make of the selected image. The file is decoded once per selection (a slider drag must not decode it per frame),
        // the line is made again only when the box or a setting changes
        uint32_t _boundsRun = 0;
        String _boundsPath;
        bool _boundsRead = false;
        Optional<ArtGenIntRect> _drawn;
        int32_t _imageWidth = 0;
        int32_t _imageHeight = 0;
        ArtGenImageSettings _summarySettings;
        bool _summaryValid = false;
        String _summaryText;
        bool _summaryIsError = false;

        // The bridge this panel started: when (ImGui time), so the line says "starting" until it polls; why it could not start
        double _bridgeStartedAt = -1.0;
        bool _bridgeWaitsForControl = false;  // Start the service turned the control API on: the bridge starts once it listens
        String _bridgeError;
        String _setupError;
    };

}  // namespace BECore
