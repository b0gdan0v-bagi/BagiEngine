#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenPanelModel.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/Icons/IconsLucide.h>

namespace BECore {

    namespace {

        constexpr StringView kNoPreset = "(no preset)";
        constexpr StringView kDirtyMark = " *";
        constexpr StringView kEngineSource = "engine";
        constexpr StringView kGameSource = "game";
        constexpr StringView kUnsavedChanges = "unsaved changes";
        constexpr StringView kNotSavedYet = "not saved yet";

        /// The longest prefix of @p text within @p limit bytes that does not end inside a UTF-8 sequence
        StringView CutAtBoundary(StringView text, size_t limit) {
            if (text.size() <= limit) {
                return text;
            }
            constexpr unsigned char kContinuationMask = 0xC0;
            constexpr unsigned char kContinuation = 0x80;
            size_t end = limit;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & kContinuationMask) == kContinuation) {
                --end;
            }
            return text.substr(0, end);
        }

        /// Copies a text the view holds into a buffer of the form, cut at a character boundary to fit with its terminator; true when the buffer changed
        template <size_t TSize>
        bool CopyIntoBuffer(StringView text, char (&buffer)[TSize]) {
            if (text == StringView(buffer)) {
                return false;
            }
            const StringView cut = CutAtBoundary(text, TSize - 1);
            std::copy(cut.begin(), cut.end(), buffer);
            buffer[cut.size()] = '\0';
            return true;
        }

        int32_t IndexOfMaxSize(int32_t maxSize) {
            const auto& options = ArtGenImageSettings::kMaxSizeOptions;
            for (size_t i = 0; i < options.size(); ++i) {
                if (options[i] == maxSize) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        int32_t IndexOfMode(StringView mode) {
            for (size_t i = 0; i < kArtGenReferenceModes.size(); ++i) {
                if (kArtGenReferenceModes[i].mode == mode) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        String MaxSizeText(int32_t maxSize) {
            return maxSize == 0 ? String("original size") : Format("{} px", maxSize);
        }

        int32_t DefaultSideOf(const ArtGenWorkflow& workflow, StringView name) {
            const ArtGenWorkflowInput* input = workflow.FindInput(name);
            return input != nullptr && input->defaultValue.kind == ArtGenInputKind::Integer ? static_cast<int32_t>(input->defaultValue.integer) : 0;
        }

    }  // namespace

    void ArtGenPanelModel::SetFormMessage(String message, bool isError) {
        _formMessage = std::move(message);
        _formMessageIsError = isError;
        _presetStale = true;
    }

    void ArtGenPanelModel::SaveForm(StringView name) {
        auto saved = GetSession().SavePreset(name, StringView(_form.ToJsonText()));
        if (!saved) {
            SetFormMessage(std::move(saved.error()), true);
            return;
        }
        CopyIntoBuffer(name, _form.name);
        _form.MarkSaved();
        SetFormMessage(Format("Saved {}", *saved), false);
    }

    void ArtGenPanelModel::LoadFirstPreset() {
        if (!_form.IsEmpty()) {
            return;
        }
        const ArtGenServiceStatus& status = GetSession().GetServiceStatus();
        for (const ArtGenPreset& preset : status.presets) {
            if (preset.error.empty() && !preset.data.empty()) {
                _form.Load(preset);
                return;
            }
        }
        if (!status.workflows.empty()) {
            _form.New(StringView(status.workflows.front().name));
        }
    }

    void ArtGenPanelModel::RunPreset(StringView argument) {
        ArtGenSession& session = GetSession();
        const auto& workflows = session.GetServiceStatus().workflows;
        if (argument == ArtGenPanelArgs::kNew) {
            if (!workflows.empty()) {
                _form.New(StringView(workflows.front().name));
                SetFormMessage(String(), false);
            }
            return;
        }
        if (_form.IsEmpty()) {
            return;
        }
        if (argument == ArtGenPanelArgs::kDuplicate) {
            _form.Duplicate();
            SetFormMessage(String(), false);
        } else if (argument == ArtGenPanelArgs::kSave) {
            SaveForm(StringView(_form.name));
        } else if (argument == ArtGenPanelArgs::kSaveAs) {
            if (IsValidSpriteName(StringView(_view.preset.saveAsName))) {
                SaveForm(StringView(_view.preset.saveAsName));
            }
        } else if (argument == ArtGenPanelArgs::kRevert) {
            if (_form.HasBase() && _form.IsDirty()) {
                _form.Revert();
                SetFormMessage(String(), false);
            }
        } else if (argument == ArtGenPanelArgs::kResetSize) {
            _form.width = 0;
            _form.height = 0;
            _form.Touch();
        } else if (argument == ArtGenPanelArgs::kAcceptDefaults) {
            _form.ClearImageAccept();
        } else if (argument == ArtGenPanelArgs::kTakeSlot) {
            if (const Optional<ArtGenReference>& slot = session.GetReferenceSlot()) {
                _form.SetReference(*slot);
            }
        } else if (argument == ArtGenPanelArgs::kRemoveReference) {
            _form.ClearReference();
        }
    }

    void ArtGenPanelModel::OpenSaveAs() {
        _view.preset.saveAsName = _form.name;
    }

    void ArtGenPanelModel::InitPresetRows() {
        for (const int32_t option : ArtGenImageSettings::kMaxSizeOptions) {
            _view.preset.accept.maxSizes.push_back(MaxSizeText(option));
        }
        for (const ArtGenReferenceModeRow& mode : kArtGenReferenceModes) {
            ArtGenComboRow& row = _view.presetRef.modes.emplace_back();
            row.label = String(mode.mode);
            row.tooltip = String(mode.tip);
        }
    }

    void ArtGenPanelModel::ApplyPresetEdits() {
        // A view made from an older form (a command or a slot changed it since) holds no edit: it would undo that change
        if (_form.IsEmpty() || _form.GetRevision() != _formRevision) {
            return;
        }
        ArtGenPresetView& view = _view.preset;
        ArtGenSession& session = GetSession();
        const ArtGenServiceStatus& status = session.GetServiceStatus();
        if (view.index != _shownIndex) {
            // Picking another preset drops what the form had unsaved, so no other edit of this frame is applied
            const bool picked = view.index >= 0 && static_cast<size_t>(view.index) < status.presets.size() && !status.presets[view.index].data.empty();
            if (picked) {
                _form.Load(status.presets[view.index]);
                SetFormMessage(String(), false);
                return;
            }
            view.index = _shownIndex;
        }
        bool touched = CopyIntoBuffer(StringView(view.name), _form.name);
        touched = CopyIntoBuffer(StringView(view.title), _form.title) || touched;
        touched = CopyIntoBuffer(StringView(view.prompt), _form.prompt) || touched;
        touched = CopyIntoBuffer(StringView(view.negative), _form.negative) || touched;
        if (view.hasSize && (view.width != _shownWidth || view.height != _shownHeight)) {
            _form.width = std::clamp(view.width, 1, ArtGenPresetForm::kMaxImageSide);
            _form.height = std::clamp(view.height, 1, ArtGenPresetForm::kMaxImageSide);
            touched = true;
        }
        if (touched) {
            _form.Touch();
        }
        if (view.workflowIndex != _shownWorkflowIndex && view.workflowIndex >= 0 && static_cast<size_t>(view.workflowIndex) < _workflowOfRow.size()) {
            const ArtGenWorkflow& workflow = status.workflows[_workflowOfRow[view.workflowIndex]];
            if (workflow.error.empty()) {
                _form.SetWorkflow(StringView(workflow.name), &workflow);
            }
        }

        ArtGenImageSettings settings = _form.GetImageAccept();
        const ArtGenImageSettingsView& accept = view.accept;
        settings.trim = accept.trim;
        settings.padding = std::clamp(accept.padding, ArtGenImageSettings::kMinPadding, ArtGenImageSettings::kMaxPadding);
        if (accept.maxSizeIndex != _shownMaxSizeIndex && accept.maxSizeIndex >= 0 && static_cast<size_t>(accept.maxSizeIndex) < ArtGenImageSettings::kMaxSizeOptions.size()) {
            settings.maxSize = ArtGenImageSettings::kMaxSizeOptions[accept.maxSizeIndex];
        }
        if (settings != _form.GetImageAccept()) {
            _form.SetImageAccept(settings);
        }
        ApplyPresetRefEdits();
    }

    void ArtGenPanelModel::ApplyPresetRefEdits() {
        const Optional<ArtGenReference>& own = _form.GetReference();
        if (!own) {
            return;
        }
        const ArtGenPresetRefView& view = _view.presetRef;
        const bool modeMoved = view.modeIndex != _shownRefModeIndex && view.modeIndex >= 0 && static_cast<size_t>(view.modeIndex) < kArtGenReferenceModes.size();
        const bool imageMoved = StringView(view.image) != StringView(own->image);
        const bool weightMoved = view.weight != _shownRefWeight;
        if (!modeMoved && !imageMoved && !weightMoved) {
            return;
        }
        ArtGenReference changed = *own;
        if (imageMoved) {
            changed.image = view.image;
        }
        if (modeMoved) {
            // The default weight of the new mode: a weight of the old one means something else there
            changed.mode = String(kArtGenReferenceModes[view.modeIndex].mode);
            changed.weight.reset();
        } else if (weightMoved) {
            const ArtGenReferenceModeRow& row = ArtGenReferenceModeOf(StringView(own->mode));
            changed.weight = static_cast<double>(std::clamp(view.weight, row.minWeight, row.maxWeight));
        }
        _form.SetReference(changed);
    }

    void ArtGenPanelModel::RefreshWorkflowRows(const ArtGenWorkflow* current) {
        ArtGenPresetView& view = _view.preset;
        const auto& workflows = GetSession().GetServiceStatus().workflows;
        view.workflows.clear();
        _workflowOfRow.clear();
        view.workflowIndex = -1;
        for (size_t i = 0; i < workflows.size(); ++i) {
            const ArtGenWorkflow& workflow = workflows[i];
            // A variant that a map of its base reaches is not picked by hand, unless the preset already names it
            if (workflow.IsReferenceVariant() && &workflow != current) {
                continue;
            }
            if (&workflow == current) {
                view.workflowIndex = static_cast<int32_t>(view.workflows.size());
            }
            ArtGenComboRow& row = view.workflows.emplace_back();
            row.label = Format("{}{}  ({}, {})", workflow.error.empty() ? "" : ICON_LC_CIRCLE_ALERT " ", workflow.name, workflow.kind, workflow.source);
            row.enabled = workflow.error.empty();
            row.tooltip = workflow.error;
            _workflowOfRow.push_back(static_cast<int32_t>(i));
        }
    }

    void ArtGenPanelModel::RefreshPreset() {
        ArtGenSession& session = GetSession();
        const ArtGenServiceStatus& status = session.GetServiceStatus();
        ArtGenPresetView& view = _view.preset;
        const bool empty = _form.IsEmpty();
        const bool dirty = !empty && _form.IsDirty();

        view.rows.clear();
        view.index = -1;
        for (size_t i = 0; i < status.presets.size(); ++i) {
            const ArtGenPreset& preset = status.presets[i];
            ArtGenComboRow& row = view.rows.emplace_back();
            row.label = Format("{}{}{}  ({}{})", preset.error.empty() ? "" : ICON_LC_CIRCLE_ALERT " ", StringView(preset.kind) == kKindAudio ? ICON_LC_AUDIO_LINES " " : "", preset.title, preset.name,
                               StringView(preset.source) == kEngineSource ? ", engine" : "");
            row.enabled = !preset.data.empty();
            row.tooltip = preset.error;
            if (!empty && preset.name == _form.GetBaseName()) {
                view.index = static_cast<int32_t>(i);
            }
        }
        _shownIndex = view.index;
        view.preview = empty ? String(kNoPreset) : Format("{}{}", StringView(_form.name), dirty ? kDirtyMark : StringView());
        view.hasForm = !empty;
        view.canNew = !status.workflows.empty();
        view.dirty = dirty;
        view.canRevert = _form.HasBase() && dirty;
        view.savedHere = !dirty && StringView(_form.GetBaseSource()) == kGameSource;
        view.engineStarter = !empty && !dirty && StringView(_form.GetBaseSource()) == kEngineSource;
        view.stateText = String(_form.HasBase() ? kUnsavedChanges : kNotSavedYet);
        view.message = _formMessage;
        view.hasMessage = !_formMessage.empty();
        view.messageStyle = _formMessageIsError ? PanelTextStyle::Error : PanelTextStyle::Success;
        const ArtGenPreset* base = session.FindPreset(StringView(_form.GetBaseName()));
        view.hasBaseError = base != nullptr && !base->error.empty() && !dirty;
        view.baseError = view.hasBaseError ? base->error : String();
        view.noList = status.presets.empty() && status.workflows.empty();

        view.name = _form.name;
        view.title = _form.title;
        view.prompt = _form.prompt;
        view.negative = _form.negative;

        const ArtGenWorkflow* workflow = empty ? nullptr : session.FindWorkflow(StringView(_form.GetWorkflow()));
        RefreshWorkflowRows(workflow);
        _shownWorkflowIndex = view.workflowIndex;
        view.workflowPreview = _form.GetWorkflow();
        view.knownWorkflow = workflow != nullptr;
        view.unknownWorkflow = !empty && workflow == nullptr;
        view.unknownWorkflowText = view.unknownWorkflow ? Format("The service does not know the workflow '{}'", _form.GetWorkflow()) : String();
        view.isAudio = workflow != nullptr && StringView(workflow->kind) == kKindAudio;
        view.hasSize = workflow != nullptr && workflow->HasSize();
        view.width = _form.width > 0 ? _form.width : (workflow != nullptr ? DefaultSideOf(*workflow, "width") : 0);
        view.height = _form.height > 0 ? _form.height : (workflow != nullptr ? DefaultSideOf(*workflow, "height") : 0);
        _shownWidth = view.width;
        _shownHeight = view.height;
        view.sizeOverridden = _form.width > 0;

        const ArtGenImageSettings& settings = _form.GetImageAccept();
        ArtGenImageSettingsView& accept = view.accept;
        accept.trim = settings.trim;
        accept.padding = settings.padding;
        accept.maxSizeIndex = IndexOfMaxSize(settings.maxSize);
        accept.maxSizeText = MaxSizeText(settings.maxSize);
        accept.overridden = _form.HasImageAccept();
        _shownMaxSizeIndex = accept.maxSizeIndex;

        RefreshPresetRef();
    }

    void ArtGenPanelModel::RefreshPresetRef() {
        ArtGenPresetRefView& view = _view.presetRef;
        const Optional<ArtGenReference>& own = _form.GetReference();
        view.has = own.has_value();
        view.unreadable = _form.HasUnreadableReference();
        view.canTake = GetSession().GetReferenceSlot().has_value();
        view.canRemove = view.has || view.unreadable;
        if (!own) {
            view.mode.clear();
            view.modeIndex = -1;
            view.modeTip.clear();
            view.image.clear();
            view.imageTip.clear();
        } else {
            const ArtGenReferenceModeRow& row = ArtGenReferenceModeOf(StringView(own->mode));
            view.mode = own->mode;
            view.modeIndex = IndexOfMode(StringView(own->mode));
            view.modeTip = String(row.tip);
            view.image = own->image;
            view.imageTip = String(ArtGenReferenceFileName(StringView(own->image)));
            view.weight = static_cast<float>(own->weight.value_or(row.defaultWeight));
        }
        _shownRefModeIndex = view.modeIndex;
        _shownRefWeight = view.weight;
    }

}  // namespace BECore
