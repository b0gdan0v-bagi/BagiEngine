#include <BECore/File/File.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/ArtGen/ArtGenPanelModel.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>

namespace BECore {

    namespace {

        constexpr StringView kShapeMode = "structure";
        constexpr StringView kUseOnSelectedTip = "Put the sprite on the SpriteRendererComponent of the node selected in the Scene Editor";
        constexpr StringView kSelectANodeTip = "Select a node in the Scene Editor first";
        constexpr StringView kNothingDrawn = "Nothing is drawn: Accept would refuse this picture";
        constexpr StringView kPlaceholderName = "<name>";

    }  // namespace

    int32_t ArtGenPanelModel::ModeIndexOf(StringView mode) {
        for (size_t i = 0; i < kArtGenReferenceModes.size(); ++i) {
            if (kArtGenReferenceModes[i].mode == mode) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    void ArtGenPanelModel::InitRunRows() {
        for (const ArtGenReferenceModeRow& mode : kArtGenReferenceModes) {
            ArtGenComboRow& row = _view.ref.modes.emplace_back();
            row.label = String(mode.mode);
            row.tooltip = String(mode.tip);
        }
        _view.accept.note = Format("Trim and downscale the image as set above and write it into the game: {} + .artgen.json and the sprite in config/TextureLibrary.xml. Both are in git.",
                                   ArtGenGameFile(kPlaceholderName, kKindImage));
        _view.accept.useOnSelectedTip = String(kSelectANodeTip);
    }

    bool ArtGenPanelModel::PresetMakesImages() const {
        const ArtGenWorkflow* workflow = GetSession().FindWorkflow(StringView(_form.GetWorkflow()));
        return workflow == nullptr || StringView(workflow->kind) != kKindAudio;
    }

    void ArtGenPanelModel::RunReference(StringView argument) {
        ArtGenSession& session = GetSession();
        if (argument == ArtGenPanelArgs::kUseFile) {
            const Optional<ArtGenReference>& slot = session.GetReferenceSlot();
            const StringView mode = slot ? StringView(slot->mode) : kShapeMode;
            const StringView root(session.GetGameRoot());
            Optional<ArtGenReference> typed = ArtGenReferenceOfTypedPath(root, StringView(_view.ref.typedPath), mode);
            const String file = typed ? ArtGenReferenceFilePath(root, StringView(typed->image)) : String();
            const auto info = typed ? GetFileInfo(Path(file)) : Optional<FileInfo>();
            _view.ref.fileMissing = !info || info->kind != FileKind::File;
            if (!_view.ref.fileMissing) {
                session.SetReferenceSlot(std::move(*typed));
            }
        } else if (argument == ArtGenPanelArgs::kClear) {
            session.ClearReferenceSlot();
        } else if (argument == ArtGenPanelArgs::kUseSprite || argument == ArtGenPanelArgs::kUseShape) {
            if (!_acceptedSprite.empty()) {
                const StringView mode = argument == ArtGenPanelArgs::kUseShape ? kShapeMode : kArtGenReferenceInitMode;
                session.SetReferenceSlot(ArtGenReferenceOfSprite(StringView(_acceptedSprite), mode));
            }
        }
    }

    void ArtGenPanelModel::ApplyRefEdits() {
        ArtGenRefView& view = _view.ref;
        if (StringView(view.typedPath) != StringView(_typedPathSeen)) {
            _typedPathSeen = view.typedPath;
            view.fileMissing = false;
        }
        ArtGenSession& session = GetSession();
        const Optional<ArtGenReference>& slot = session.GetReferenceSlot();
        // A view made from another slot (Use as reference filled it since) holds no edit: it would undo that
        if (!slot || slot != _refShown) {
            return;
        }
        const ArtGenReferenceModeRow& row = ArtGenReferenceModeOf(StringView(slot->mode));
        const float shownWeight = static_cast<float>(slot->weight.value_or(row.defaultWeight));
        const bool modeMoved = view.modeIndex != ModeIndexOf(StringView(slot->mode)) && view.modeIndex >= 0 && static_cast<size_t>(view.modeIndex) < kArtGenReferenceModes.size();
        if (modeMoved) {
            ArtGenReference changed = *slot;
            changed.mode = String(kArtGenReferenceModes[view.modeIndex].mode);
            changed.weight.reset();  // the default of the new mode
            session.SetReferenceSlot(std::move(changed));
        } else if (view.weight != shownWeight) {
            ArtGenReference changed = *slot;
            changed.weight = static_cast<double>(std::clamp(view.weight, row.minWeight, row.maxWeight));
            session.SetReferenceSlot(std::move(changed));
        }
    }

    void ArtGenPanelModel::RefreshRef() {
        ArtGenRefView& view = _view.ref;
        const Optional<ArtGenReference>& slot = GetSession().GetReferenceSlot();
        _refShown = slot;
        view.has = slot.has_value();
        if (!slot) {
            view.name.clear();
            view.mode.clear();
            view.modeIndex = -1;
            view.modeTip.clear();
            view.weight = 0.0f;
            return;
        }
        const ArtGenReferenceModeRow& row = ArtGenReferenceModeOf(StringView(slot->mode));
        view.name = String(ArtGenReferenceFileName(StringView(slot->image)));
        view.mode = slot->mode;
        view.modeIndex = ModeIndexOf(StringView(slot->mode));
        view.modeTip = String(row.tip);
        view.weight = static_cast<float>(slot->weight.value_or(row.defaultWeight));
    }

    void ArtGenPanelModel::RunRun(StringView argument) {
        ArtGenSession& session = GetSession();
        _runStale = true;
        if (argument == ArtGenPanelArgs::kCancel) {
            if (session.IsSingleRunInWork()) {
                session.Cancel();
            }
            return;
        }
        if (argument != ArtGenPanelArgs::kGenerate) {
            return;
        }
        ArtGenRunView& view = _view.run;
        if (session.IsBusy() || _form.IsEmpty() || view.subject.empty()) {
            return;
        }
        const int32_t count = std::clamp(view.count, 1, ArtGenSession::kMaxCount);
        const Optional<int64_t> seed = view.randomSeed ? Optional<int64_t>() : Optional<int64_t>(std::max<int64_t>(view.seed, 0));
        // A saved preset goes by name, so its provenance names the file; anything else goes inline as a draft
        const Optional<ArtGenReference> reference = PresetMakesImages() ? session.GetReferenceSlot() : Optional<ArtGenReference>();
        auto result = _form.IsDirty() ? session.RequestDraft(StringView(_form.name), StringView(_form.ToJsonText()), StringView(view.subject), count, seed, view.keepVariants, reference)
                                      : session.Request(StringView(_form.GetBaseName()), StringView(view.subject), count, seed, view.keepVariants, reference);
        _requestError = result ? String() : result.error();
    }

    void ArtGenPanelModel::UpdateRun(bool sessionMoved) {
        ArtGenSession& session = GetSession();
        ArtGenRunView& view = _view.run;
        view.count = std::clamp(view.count, 1, ArtGenSession::kMaxCount);
        view.seed = std::max<int64_t>(view.seed, 0);
        view.images = PresetMakesImages();
        view.canGenerate = !session.IsBusy() && !_form.IsEmpty() && !view.subject.empty();
        if (!view.images) {
            _referencePanel.DropThumbnail();
        }
        if (sessionMoved || _runStale) {
            _runStale = false;
            RefreshRunTexts();
            RefreshRef();
        }
        const ArtGenRun& run = session.GetRun();
        if (run.setName.empty()) {
            SyncThumbnails(run);
        } else {
            // The last job of a set stays in the run: its variants are the set's drafts, judged and accepted on the Set tab
            DropThumbnails();
        }
    }

    void ArtGenPanelModel::RefreshRunTexts() {
        ArtGenSession& session = GetSession();
        const ArtGenRun& run = session.GetRun();
        ArtGenRunView& view = _view.run;
        view.singleRunInWork = session.IsSingleRunInWork();
        view.cancelRequested = run.cancelRequested;
        view.setRunning = session.IsBusy() && !view.singleRunInWork;
        view.requestError = _requestError;
        view.hasRequestError = !_requestError.empty();
        view.state = run.state;
        view.isSetJob = !run.setName.empty();
        view.setJobText = view.isSetJob ? Format("job {} of set {} (see the Set tab)", run.jobName, run.setName) : String();
        const int32_t made = static_cast<int32_t>(run.variants.size()) - run.keptVariants;
        const float perImage = run.steps > 0 ? static_cast<float>(run.step) / static_cast<float>(run.steps) : 0.0f;
        view.progress = run.count > 0 ? (static_cast<float>(made) + perImage) / static_cast<float>(run.count) : 0.0f;
        view.progressText = run.state == ArtGenRunState::Running ? Format("image {} of {}, step {} of {}", made + 1, run.count, run.step, run.steps) : String();
        view.doneText = run.state == ArtGenRunState::Done ? Format("{} image(s) of \"{}\"", run.variants.size(), run.subject) : String();
        view.failedText = run.state == ArtGenRunState::Failed ? Format("Failed: {}", run.error) : String();
        view.cancelledText = run.state == ArtGenRunState::Cancelled ? Format("Cancelled; {} image(s) kept", run.variants.size()) : String();
        view.hasSounds = std::any_of(run.variants.begin(), run.variants.end(), [](const ArtGenVariant& variant) { return variant.IsAudio(); });
        view.hasImages = !view.hasSounds && !run.variants.empty();
    }

    void ArtGenPanelModel::UpdateAccept() {
        ArtGenSession& session = GetSession();
        ArtGenAcceptView& view = _view.accept;
        const ArtGenVariant* variant = _selectedSeed ? session.FindVariant(*_selectedSeed) : nullptr;
        const bool haveNode = session.GetSelectedNodeIfAlive() != nullptr;
        if (haveNode != view.haveNode) {
            view.haveNode = haveNode;
            view.useOnSelectedTip = String(haveNode ? kUseOnSelectedTip : kSelectANodeTip);
        }
        if (!haveNode) {
            view.useOnSelected = false;
        }
        view.spriteValid = IsValidSpriteName(StringView(view.sprite));
        view.nameWarning = !view.spriteValid && !view.sprite.empty();
        view.hasPick = variant != nullptr;
        view.hasAccepted = !_acceptedSprite.empty() && _view.run.images;
        if (_acceptStale || _acceptShownSeed != _selectedSeed) {
            _acceptStale = false;
            _acceptShownSeed = _selectedSeed;
            view.seedText = _selectedSeed ? Format("seed {}", *_selectedSeed) : String();
            view.message = _acceptMessage;
            view.hasMessage = !_acceptMessage.empty();
            view.messageStyle = _acceptFailed ? PanelTextStyle::Error : PanelTextStyle::Success;
        }
        if (variant != nullptr && !variant->IsAudio()) {
            RefreshAcceptSummary(session.GetRun(), *variant);
        }
    }

    void ArtGenPanelModel::RefreshAcceptSummary(const ArtGenRun& run, const ArtGenVariant& variant) {
        ArtGenAcceptView& view = _view.accept;
        if (!_boundsRead || _boundsRun != run.id || _boundsPath != variant.path) {
            _boundsRead = true;
            _boundsRun = run.id;
            _boundsPath = variant.path;
            _summaryValid = false;
            _drawn.reset();
            view.summary.clear();
            auto pixels = ReadArtGenPng(Path(variant.path));
            if (pixels) {
                _imageWidth = static_cast<int32_t>(pixels->width);
                _imageHeight = static_cast<int32_t>(pixels->height);
                _drawn = DrawnBounds(*pixels);
                if (!_drawn) {
                    view.summary = String(kNothingDrawn);
                    view.summaryStyle = PanelTextStyle::Error;
                    _summaryValid = true;
                }
            } else {
                view.summary = pixels.error();
                view.summaryStyle = PanelTextStyle::Error;
                _summaryValid = true;
            }
            view.hasSummary = !view.summary.empty();
        }
        const ArtGenImageSettings& settings = _form.GetImageAccept();
        if (_summaryValid && (!_drawn || _summarySettings == settings)) {
            return;
        }
        _summarySettings = settings;
        _summaryValid = true;
        const ArtGenIntRect bounds = *_drawn;
        auto frame = MakeArtGenFrame(Span<const ArtGenIntRect>(&bounds, 1), _imageWidth, _imageHeight, settings);
        view.summaryStyle = frame ? PanelTextStyle::Disabled : PanelTextStyle::Error;
        view.summary = frame ? Format("drawn {} x {}, written {} x {}", bounds.w, bounds.h, frame->outWidth, frame->outHeight) : frame.error();
        view.hasSummary = !view.summary.empty();
    }

    void ArtGenPanelModel::RunAccept(StringView argument) {
        if (argument == ArtGenPanelArgs::kSound) {
            AcceptSound();
            return;
        }
        if (argument != ArtGenPanelArgs::kImage || !_selectedSeed) {
            return;
        }
        ArtGenAcceptView& view = _view.accept;
        if (!IsValidSpriteName(StringView(view.sprite))) {
            return;
        }
        const ArtGenImageSettings settings = _form.GetImageAccept();
        const bool useOnSelected = view.useOnSelected && GetSession().GetSelectedNodeIfAlive() != nullptr;
        NoteImageAccepted(StringView(view.sprite), GetSession().Accept(*_selectedSeed, StringView(view.sprite), useOnSelected, &settings));
    }

    void ArtGenPanelModel::OnVariantDropped(const ArtGenVariantDropEvent& event) {
        if (event.result == nullptr) {
            return;
        }
        ArtGenSession& session = GetSession();
        const ArtGenDragPayload& payload = event.payload;
        if (payload.kind == ArtGenDragKind::Audio) {
            *event.result = ArtGenDropResult{false, String(kArtGenSoundIsNotASprite)};
            return;
        }
        std::expected<ArtGenAcceptAnswer, ControlError> result;
        if (payload.set[0] == '\0') {
            // Exactly the Accept button: the form's settings, the preset's atlas group, no node
            const ArtGenImageSettings settings = _form.GetImageAccept();
            result = session.Accept(payload.seed, event.sprite, false, &settings);
        } else {
            result = session.AcceptSetVariant(StringView(payload.set), StringView(payload.job), payload.seed, event.sprite);
        }
        NoteImageAccepted(event.sprite, result);
        *event.result = ArtGenDropResult{result.has_value(), _acceptMessage};
    }

    void ArtGenPanelModel::NoteImageAccepted(StringView sprite, const std::expected<ArtGenAcceptAnswer, ControlError>& result) {
        _acceptStale = true;
        _acceptFailed = !result.has_value();
        _acceptedSprite.clear();
        if (!result) {
            _acceptMessage = result.error().message;
            return;
        }
        _acceptedSprite = String(sprite);
        _acceptMessage = Format("Accepted as '{}' ({}); {} sprite(s) reloaded", sprite, result->texturePath.value_or(String()), result->reloadedSprites.value_or(0));
        if (result->atlas) {
            _acceptMessage += Format("; atlas '{}' repacked ({}x{})", result->atlas->name, result->atlas->width, result->atlas->height);
        }
    }

    void ArtGenPanelModel::ApplyNotedClicks() {
        ArtGenSession& session = GetSession();
        if (const Optional<int64_t> picked = std::exchange(_pickedSeed, Optional<int64_t>())) {
            if (_selectedSeed != picked) {
                _acceptMessage.clear();
                _acceptedSprite.clear();
                _acceptStale = true;
                _preview.Reset();
                _previewError.clear();
                _soundAcceptStale = true;
            }
            _selectedSeed = picked;
            if (_view.accept.sprite.empty()) {
                // The name field filled from the subject of the run ("White Knight!" -> "white_knight"); the user edits it before Accept
                _view.accept.sprite = ArtGenNameFromSubject(StringView(session.GetRun().subject));
            }
        }
        if (const Optional<int64_t> dice = std::exchange(_diceSeed, Optional<int64_t>())) {
            _view.run.randomSeed = false;
            _view.run.seed = *dice;
        }
        if (const Optional<int32_t> row = std::exchange(_playRow, Optional<int32_t>())) {
            if (*row < 0) {
                StopSound();
            } else if (static_cast<size_t>(*row) < _sounds.size()) {
                const SoundRow& sound = _sounds[*row];
                if (sound.clip && sound.clip->GetState() == ResourceState::Loaded) {
                    PlaySound(*sound.clip, *row);
                }
            }
        }
        if (const Optional<int64_t> seed = std::exchange(_referenceSeed, Optional<int64_t>())) {
            const ArtGenVariant* variant = session.FindVariant(*seed);
            if (variant != nullptr && !variant->IsAudio()) {
                if (auto reference = ArtGenReferenceOfVariant(StringView(session.GetGameRoot()), kKindImage, StringView(variant->path))) {
                    session.SetReferenceSlot(std::move(*reference));
                }
            }
        }
    }

    void ArtGenPanelModel::SyncThumbnails(const ArtGenRun& run) {
        // Keep the thumbnails of the paths still in the run (a run that kept the variants), drop the rest, load the new ones
        bool same = _thumbnailPaths.size() <= run.variants.size();
        for (size_t i = 0; same && i < _thumbnailPaths.size(); ++i) {
            same = _thumbnailPaths[i] == run.variants[i].path;
        }
        if (!same) {
            DropThumbnails();
        }
        auto& resources = CoreManager::GetResourceManager();
        while (_thumbnails.size() < run.variants.size()) {
            const ArtGenVariant& variant = run.variants[_thumbnails.size()];
            // A sound has no picture: its slot stays empty and the grid draws a sound tile (playing it is the sound list's)
            _thumbnails.push_back(variant.IsAudio() ? ResourceHandle<ITexture>() : resources.Load<ITexture>(StringView(variant.path.data(), variant.path.size())));
            _thumbnailPaths.push_back(variant.path);
            // Read once with the thumbnail: the service's image report carries no verdict, its provenance file does
            _verdicts.push_back(variant.IsAudio() ? ArtGenVerdict() : ReadArtGenVerdict(Path(variant.provenancePath)));
        }
    }

    void ArtGenPanelModel::DropThumbnails() {
        // The images of a run are seen once: keeping them in the cache would hold every generated texture until the editor closes
        auto& resources = CoreManager::GetResourceManager();
        for (size_t i = 0; i < _thumbnailPaths.size(); ++i) {
            if (_thumbnails[i]) {
                resources.Evict(StringView(_thumbnailPaths[i].data(), _thumbnailPaths[i].size()));
            }
        }
        _thumbnails.clear();
        _thumbnailPaths.clear();
        _verdicts.clear();
    }

}  // namespace BECore
