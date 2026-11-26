#include "ArtGenSession.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonMemberMap.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/File/File.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <EASTL/sort.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenSessionControl.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <chrono>

namespace BECore {

    using namespace ArtGenControl;

    namespace {

        double SteadySeconds() {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            return std::chrono::duration<double>(now).count();
        }

    }  // namespace

    ArtGenSession::ArtGenSession(Clock clock) : _clock(clock ? std::move(clock) : Clock(&SteadySeconds)) {}

    ArtGenSession::~ArtGenSession() = default;

    void ArtGenSession::SetSelectedNode(IntrusivePtr<SceneNode> node) {
        _selectedNode = std::move(node);
    }

    SceneNode* ArtGenSession::GetSelectedNodeIfAlive() const {
        if (!_selectedNode) {
            return nullptr;
        }
        Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
        return scene != nullptr ? HierarchyModel::FindNode(scene->GetRootNode(), _selectedNode.Get()) : nullptr;
    }

    const ArtGenVariant* ArtGenSession::FindVariant(int64_t seed) const {
        for (const auto& variant : _run.variants) {
            if (variant.seed == seed) {
                return &variant;
            }
        }
        return nullptr;
    }

    StringView ArtGenSession::PresetTextOf(StringView presetData, StringView preset) const {
        if (!presetData.empty()) {
            return presetData;
        }
        const ArtGenPreset* saved = FindPreset(preset);
        return saved != nullptr ? StringView(saved->data) : StringView();
    }

    ArtGenImageSettings ArtGenSession::ImageSettingsOf(StringView presetData, StringView preset) const {
        JsonTape tape;
        if (!tape.Parse(PresetTextOf(presetData, preset))) {
            return {};
        }
        const Optional<JsonView> accept = FindParam(JsonView(tape, 0), "accept");
        if (!accept) {
            return {};
        }
        return ReadArtGenImageSettings(*accept).value_or(ArtGenImageSettings{});
    }

    ArtGenImageSettings ArtGenSession::ImageSettingsOfRun() const {
        return ImageSettingsOf(StringView(_run.presetData), StringView(_run.preset));
    }

    String ArtGenSession::AtlasOf(StringView presetData, StringView preset) const {
        JsonTape tape;
        if (!tape.Parse(PresetTextOf(presetData, preset))) {
            return {};
        }
        const Optional<String> group = GetParamString(JsonView(tape, 0), "atlas");
        return group && IsValidSpriteName(StringView(*group)) ? *group : String();
    }

    String ArtGenSession::AtlasOfRun() const {
        return AtlasOf(StringView(_run.presetData), StringView(_run.preset));
    }

    std::expected<ArtGenAcceptAnswer, ControlError> ArtGenSession::Accept(int64_t seed, StringView sprite, bool useOnSelected, const ArtGenImageSettings* settings, const String* atlas) {
        const ArtGenVariant* variant = FindVariant(seed);
        if (variant == nullptr) {
            return std::unexpected(ControlError{String(kNoVariantCode), Format("the current run has no image of seed {}", seed)});
        }
        if (variant->IsAudio()) {
            return std::unexpected(ControlError{String(kAcceptFailedCode), String("a sound is accepted as a clip (name or clip), not a sprite")});
        }
        if (_acceptor == nullptr) {
            return std::unexpected(ControlError{String(kAcceptFailedCode), String("accepting is not available here")});
        }
        SceneNode* node = nullptr;
        if (useOnSelected) {
            node = GetSelectedNodeIfAlive();
            if (node == nullptr) {
                return std::unexpected(ControlError{String(kAcceptFailedCode), String("no node is selected in the scene editor")});
            }
        }
        ArtGenImagePlan plan{settings != nullptr ? *settings : ImageSettingsOfRun(), {}};
        plan.atlas = atlas != nullptr ? *atlas : AtlasOfRun();
        return _acceptor(*variant, sprite, plan, node);
    }

    std::expected<ArtGenAcceptAnswer, ControlError> ArtGenSession::AcceptSound(int64_t seed, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, bool useOnSelected) {
        const ArtGenVariant* variant = FindVariant(seed);
        if (variant == nullptr) {
            return std::unexpected(ControlError{String(kNoVariantCode), Format("the current run has no sound of seed {}", seed)});
        }
        if (!variant->IsAudio()) {
            return std::unexpected(ControlError{String(kAcceptFailedCode), String("an image is accepted as a sprite, not a clip")});
        }
        if (_soundAcceptor == nullptr) {
            return std::unexpected(ControlError{String(kAcceptFailedCode), String("accepting a sound is not available here")});
        }
        SceneNode* node = nullptr;
        if (useOnSelected) {
            node = GetSelectedNodeIfAlive();
            if (node == nullptr) {
                return std::unexpected(ControlError{String(kAcceptFailedCode), String("no node is selected in the scene editor")});
            }
        }
        return _soundAcceptor(*variant, target, settings, node);
    }

    void ArtGenSession::RegisterControl(ControlService& service) {
        if (_registeredOn == &service) {
            return;
        }
        _registeredOn = &service;
        Register<&ArtGenSession::HandlePoll>(service, *this, "artgen_poll");
        Register<&ArtGenSession::HandleProgress>(service, *this, "artgen_progress");
        Register<&ArtGenSession::HandleImage>(service, *this, "artgen_image");
        Register<&ArtGenSession::HandleDone>(service, *this, "artgen_done");
        Register<&ArtGenSession::HandleGenerate>(service, *this, "artgen_generate");
        Register<&ArtGenSession::HandleCancel>(service, *this, "artgen_cancel");
        Register<&ArtGenSession::HandleAccept>(service, *this, "artgen_accept");
        Register<&ArtGenSession::HandleSetupReport>(service, *this, "artgen_setup_report");
        Register<&ArtGenSession::HandleSetup>(service, *this, "artgen_setup");
        service.RegisterHandler("artgen_state", [this](ControlCall& call) { call.ReplyObject(BuildState()); });
        RegisterSetControl(service);

        service.DescribeMethod("artgen_generate",
                               ControlMethodInfo{"Asks the art generation service for images of a style preset of the game (the Generate panel's button)",
                                                 {ControlParamInfo{"preset", "string|object", true, "a preset name (the game's, else the engine's starter), or a preset object (a draft)"},
                                                  ControlParamInfo{"presetName", "string", false, "with a draft: the name it is shown under"},
                                                  ControlParamInfo{"subject", "string", true, "what to draw; replaces {subject} in the preset's prompt"},
                                                  ControlParamInfo{"count", "integer", false, "images, 1 to 16 (default 4)"},
                                                  ControlParamInfo{"seed", "integer", false, "the first seed; image i takes seed + i (default random)"},
                                                  ControlParamInfo{"reference", "object", false,
                                                                   "a picture the images start from: {mode (init), image (a path relative to the game root, or absolute), weight (0 to 1, how "
                                                                   "strongly the picture holds; the mode's default when missing)}; replaces the preset's own reference"}}});
        service.DescribeMethod(
            "artgen_accept",
            ControlMethodInfo{
                "Makes an image of the current run a sprite of the game (trimmed to what is drawn and downscaled, a PNG into assets/artgen, "
                "TextureLibrary entry, reload), or a sound a clip (processed, a WAV into assets/artgen or over a clip path of the game, reload)",
                {ControlParamInfo{"seed", "integer", true, "the seed of the image or sound (artgen_state.run.variants)"},
                 ControlParamInfo{"sprite", "string", false, "an image: the sprite name, 1 to 64 of A-Z a-z 0-9 _ -"},
                 ControlParamInfo{"image", "object", false,
                                  "an image: trim, padding (percent, 0 to 25), maxSize (pixels, 0 or 16 to 4096); a missing key keeps the preset's value (its accept key, else the "
                                  "defaults)"},
                 ControlParamInfo{"atlas", "string", false, "an image: the atlas group the sprite joins and packs into; \"\" for none; missing keeps the preset's atlas key (none when it has none)"},
                 ControlParamInfo{"name", "string", false, "a sound: the clip assets/artgen/<name>.wav"},
                 ControlParamInfo{"clip", "string", false, "a sound: instead of name, the game's clip to replace (assets/.../x.wav)"},
                 ControlParamInfo{"sound", "object", false, "a sound: mono, trimDb, maxSeconds, sampleRate, fadeInMs, fadeOutMs, peakDb (null: off)"},
                 ControlParamInfo{"useOnSelected", "boolean", false, "also put it on the node selected in the scene editor"}}});
        service.DescribeMethod(
            "artgen_cancel",
            ControlMethodInfo{"Stops the run of the art generation service (a set's running job; the set goes on); the images made stay. With job or set, cancels that job, or every job of the set",
                              {ControlParamInfo{"job", "string", false, "a job of a set: cancel it (a pending one at once, the running one through the service)"},
                               ControlParamInfo{"set", "string", false, "the set of the job (default: the only active set that has the job), or with all the set to cancel"},
                               ControlParamInfo{"all", "boolean", false, "with set: cancel every pending job and the running one"}}});
        service.DescribeMethod("artgen_state", ControlMethodInfo{"The art generation service as the editor sees it, the current run with its images, and a summary of each set", {}, /*quiet=*/true});
        service.DescribeMethod("artgen_setup", ControlMethodInfo{"Asks the art generation service to plan, install, start or stop its generator (the panel's Set up / Start / Stop)",
                                                                 {ControlParamInfo{"action", "string", true, "Plan, Install, Start, Stop, or cancel (an install in work)"}}});
        Register<&ArtGenSession::HandleSavePreset>(service, *this, "artgen_save_preset");
        service.DescribeMethod("artgen_save_preset", ControlMethodInfo{"Writes a style preset of the game (artgen/presets/<name>.json), as the panel's Save does",
                                                                       {ControlParamInfo{"name", "string", true, "1 to 64 of A-Z a-z 0-9 _ -"},
                                                                        ControlParamInfo{"preset", "object", true, "the preset (tools/artgen/CLAUDE.md, \"Presets\")"}}});
        service.DescribeMethod(
            "artgen_poll",
            ControlMethodInfo{"Called by the art generation service: its status in, the game root and a new request (with outDir and name for a job of a set) or a cancel out", {}, /*quiet=*/true});
        service.DescribeMethod("artgen_progress",
                               ControlMethodInfo{"Called by the art generation service: how far the image in work is; stale when the request is not the current one", {}, /*quiet=*/true});
        service.DescribeMethod("artgen_image", ControlMethodInfo{"Called by the art generation service: one finished image or sound of the request", {}, /*quiet=*/true});
        service.DescribeMethod("artgen_done", ControlMethodInfo{"Called by the art generation service: the request ended, with an error when it failed", {}, /*quiet=*/true});
        service.DescribeMethod("artgen_setup_report", ControlMethodInfo{"Called by the art generation service: the plan, the progress or the end of a setup action", {}, /*quiet=*/true});
    }

    std::expected<uint32_t, String> ArtGenSession::Request(StringView preset, StringView subject, int32_t count, Optional<int64_t> seed, bool keepVariants, Optional<ArtGenReference> reference) {
        for (const auto& known : _status.presets) {
            if (StringView(known.name) == preset && !known.error.empty()) {
                return std::unexpected(Format("the preset '{}' is broken: {}", preset, known.error));
            }
        }
        return Queue(preset, String(), subject, count, seed, keepVariants, std::move(reference));
    }

    std::expected<uint32_t, String> ArtGenSession::RequestDraft(StringView name, StringView presetText, StringView subject, int32_t count, Optional<int64_t> seed, bool keepVariants,
                                                                Optional<ArtGenReference> reference) {
        if (auto shape = CheckArtGenPresetText(presetText); !shape) {
            return std::unexpected(Format("the draft is not a preset: {}", shape.error()));
        }
        return Queue(name.empty() ? StringView("draft") : name, String(presetText), subject, count, seed, keepVariants, std::move(reference));
    }

    void ArtGenSession::SetReferenceSlot(ArtGenReference reference) {
        if (_referenceSlot && *_referenceSlot == reference) {
            return;
        }
        _referenceSlot = std::move(reference);
        ++_version;
    }

    void ArtGenSession::ClearReferenceSlot() {
        if (_referenceSlot) {
            _referenceSlot.reset();
            ++_version;
        }
    }

    std::expected<uint32_t, String> ArtGenSession::Queue(StringView preset, String presetData, StringView subject, int32_t count, Optional<int64_t> seed, bool keepVariants,
                                                         Optional<ArtGenReference> reference) {
        if (IsBusy()) {
            return std::unexpected(String("a run is in work; cancel it or wait"));
        }
        if (IsSetupBusy()) {
            return std::unexpected(String("the generator is being set up; wait for it"));
        }
        if (preset.empty()) {
            return std::unexpected(String("no preset"));
        }
        if (count < 1 || count > kMaxCount) {
            return std::unexpected(Format("count must be 1 to {}", kMaxCount));
        }
        if (seed && *seed < 0) {
            return std::unexpected(String("seed must not be negative"));
        }
        // The variants of a set's job belong to its row in the manifest, so the next single run does not carry them
        Vector<ArtGenVariant> kept;
        if (keepVariants && _run.setName.empty()) {
            kept = std::move(_run.variants);
        }
        _run = ArtGenRun{};
        _run.variants = std::move(kept);
        _run.keptVariants = static_cast<int32_t>(_run.variants.size());
        _run.id = _nextId++;
        _run.state = ArtGenRunState::Queued;
        _run.preset = String(preset);
        _run.presetData = std::move(presetData);
        _run.subject = String(subject);
        _run.count = count;
        _run.seed = seed;
        _run.reference = std::move(reference);
        ++_version;
        return _run.id;
    }

    void ArtGenSession::Cancel() {
        if (_run.state == ArtGenRunState::Queued) {
            // The service never saw it: nothing to stop there
            _run.state = ArtGenRunState::Cancelled;
            ++_version;
            EndSetJob("cancelled");
            PumpSets();
        } else if (_run.state == ArtGenRunState::Running && !_run.cancelRequested) {
            _run.cancelRequested = true;
            ++_version;
        }
    }

    std::expected<uint32_t, String> ArtGenSession::RequestSetup(ArtGenSetupAction action) {
        if (action == ArtGenSetupAction::None) {
            return std::unexpected(String("no action"));
        }
        if (IsSetupBusy()) {
            return std::unexpected(String("a setup action is in work; wait for it"));
        }
        if (IsBusy()) {
            return std::unexpected(String("a run is in work; cancel it or wait"));
        }
        ArtGenSetup next;
        if (action == ArtGenSetupAction::Install) {
            // The plan the owner agreed to stays on screen while the install runs
            next.platform = std::move(_setup.platform);
            next.platformNote = std::move(_setup.platformNote);
            next.root = std::move(_setup.root);
            next.plan = std::move(_setup.plan);
            next.downloadBytes = _setup.downloadBytes;
        }
        _setup = std::move(next);
        _setup.id = _nextId++;
        _setup.action = action;
        _setup.state = ArtGenSetupState::Queued;
        ++_version;
        return _setup.id;
    }

    const ArtGenWorkflow* ArtGenSession::FindWorkflow(StringView name) const {
        for (const auto& workflow : _status.workflows) {
            if (StringView(workflow.name) == name) {
                return &workflow;
            }
        }
        return nullptr;
    }

    const ArtGenPreset* ArtGenSession::FindPreset(StringView name) const {
        for (const auto& preset : _status.presets) {
            if (StringView(preset.name) == name) {
                return &preset;
            }
        }
        return nullptr;
    }

    std::expected<String, String> ArtGenSession::SavePreset(StringView name, StringView presetText) {
        if (!IsValidSpriteName(name)) {
            return std::unexpected(String("a preset name is 1 to 64 of A-Z a-z 0-9 _ -"));
        }
        JsonTape tape;
        if (!tape.Parse(presetText)) {
            return std::unexpected(String("a preset is a JSON object"));
        }
        const JsonView preset(tape, 0);
        if (auto shape = CheckArtGenPresetShape(preset); !shape) {
            return std::unexpected(shape.error());
        }
        if (_gameRoot.empty()) {
            return std::unexpected(String("no game root to save into"));
        }
        const Path folder = Path(StringView(_gameRoot)) / "artgen" / "presets";
        if (auto made = CreateDirectories(folder); !made) {
            return std::unexpected(Format("cannot make {}: {}", folder.View(), made.error()));
        }
        const Path path = folder / Format("{}.json", name);
        if (auto written = WriteFileAtomically(path, FormatArtGenPresetFile(preset)); !written) {
            return std::unexpected(written.error());
        }
        // Shown at once as the game's own; the service checks it at the look the next poll asks for
        ArtGenPreset saved;
        saved.name = String(name);
        saved.title = GetParamString(preset, "title").value_or(saved.name);
        saved.source = "game";
        if (const ArtGenWorkflow* workflow = FindWorkflow(StringView(GetParamString(preset, "workflow").value_or(String()))); workflow != nullptr) {
            saved.kind = workflow->kind;
        }
        saved.data = String(presetText);
        auto existing = std::find_if(_status.presets.begin(), _status.presets.end(), [&](const ArtGenPreset& known) { return StringView(known.name) == name; });
        if (existing != _status.presets.end()) {
            *existing = std::move(saved);
        } else {
            _status.presets.push_back(std::move(saved));
        }
        _refreshCatalog = true;
        ++_version;
        return String(path.View());
    }

    void ArtGenSession::RequestServiceStop() {
        if (IsServiceConnected() && !_stopService) {
            _stopService = true;
            ++_version;
        }
    }

    void ArtGenSession::ClearSetup() {
        if (!IsSetupBusy() && _setup.action != ArtGenSetupAction::None) {
            _setup = ArtGenSetup{};
            ++_version;
        }
    }

    void ArtGenSession::CancelSetup() {
        if (_setup.state == ArtGenSetupState::Queued) {
            _setup.state = ArtGenSetupState::Failed;
            _setup.error = "cancelled";
            ++_version;
        } else if (_setup.state == ArtGenSetupState::Running && !_setup.cancelRequested) {
            _setup.cancelRequested = true;
            ++_version;
        }
    }

    bool ArtGenSession::IsServiceConnected() const {
        return _lastPollSeconds >= 0.0 && _clock() - _lastPollSeconds <= kServiceTimeoutSeconds;
    }

    bool ArtGenSession::IsGeneratorReady() const {
        return IsServiceConnected() && StringView(_status.state) == kGeneratorReady;
    }

    // By hand (ArtGenSession.h, `HandlePoll`): `request.preset` is a name or the text of an inline draft in the middle of the reply
    std::expected<String, ControlError> ArtGenSession::HandlePoll(JsonView params) {
        if (!params.IsObject()) {
            return std::unexpected(InvalidParams("params must be an object"));
        }
        ArtGenServiceStatus status;
        status.state = StringOr(params, "state", kGeneratorReady);
        status.reason = StringOr(params, "reason");
        status.generator = StringOr(params, "generator");
        status.startedHere = GetParamBool(params, "startedHere").value_or(false);
        const bool wasConnected = IsServiceConnected();
        const bool changed = status.state != _status.state || status.reason != _status.reason || status.generator != _status.generator || status.startedHere != _status.startedHere;
        status.presets = std::move(_status.presets);
        status.workflows = std::move(_status.workflows);
        _status = std::move(status);
        ReadCatalog(params);
        _lastPollSeconds = _clock();
        ++_pollCount;
        if (changed || !wasConnected) {
            ++_version;
        }

        // What the reply says is decided first and written once, so its keys come out in byte order whatever the order of the decisions
        const bool refresh = std::exchange(_refreshCatalog, false);
        const bool shutdown = std::exchange(_stopService, false);
        bool sendRequest = false;
        bool sendSetup = false;
        Optional<uint32_t> cancel;
        Optional<uint32_t> setupCancel;
        if (shutdown) {
            // The service exits on this answer and reports nothing more: what it was doing ends here, and it is gone at once
            if (_run.state == ArtGenRunState::Running) {
                _run.state = ArtGenRunState::Cancelled;
                _run.step = 0;
                // Only the running job ends; the others stay Pending and go on when a service polls again
                EndSetJob("the service was stopped");
            }
            if (_setup.state == ArtGenSetupState::Running) {
                _setup.state = ArtGenSetupState::Failed;
                _setup.error = "the service was stopped";
            }
            _lastPollSeconds = -1.0;
            ++_version;
        } else {
            PumpSets();
            if (_run.state == ArtGenRunState::Queued) {
                sendRequest = true;
                _run.state = ArtGenRunState::Running;
                ++_version;
            } else if (_run.state == ArtGenRunState::Running && _run.cancelRequested) {
                cancel = _run.id;
            }
            if (_setup.state == ArtGenSetupState::Queued) {
                sendSetup = true;
                _setup.state = ArtGenSetupState::Running;
                ++_version;
            } else if (_setup.state == ArtGenSetupState::Running && _setup.cancelRequested) {
                setupCancel = _setup.id;
            }
        }

        String reply;
        JsonWriter writer(reply);
        writer.BeginObject();
        if (cancel) {
            writer.Member("cancel", *cancel);
        }
        writer.Member("gameRoot", StringView(_gameRoot));
        if (refresh) {
            writer.Member("refresh", true);
        }
        if (sendRequest) {
            const bool draft = !_run.presetData.empty();
            writer.Key("request");
            writer.BeginObject();
            writer.Member("count", _run.count);
            writer.Member("id", _run.id);
            if (!_run.outDir.empty()) {
                writer.Member("name", StringView(_run.name));
                writer.Member("outDir", StringView(_run.outDir));
            }
            writer.Key("preset");
            if (draft) {
                writer.Raw(StringView(_run.presetData));
            } else {
                writer.Value(StringView(_run.preset));
            }
            if (draft) {
                writer.Member("presetName", StringView(_run.preset));
            }
            if (_run.reference) {
                writer.Key("reference");
                WriteArtGenReference(writer, *_run.reference);
            }
            writer.Key("seed");
            if (_run.seed) {
                writer.Value(*_run.seed);
            } else {
                writer.Null();
            }
            writer.Member("subject", StringView(_run.subject));
            writer.EndObject();
        }
        if (sendSetup) {
            writer.Key("setup");
            writer.BeginObject();
            writer.Member("action", EnumUtils<ArtGenSetupAction>::ToString(_setup.action));
            writer.Member("id", _setup.id);
            writer.EndObject();
        }
        if (setupCancel) {
            writer.Member("setupCancel", *setupCancel);
        }
        if (shutdown) {
            writer.Member("shutdown", true);
        }
        writer.EndObject();
        return reply;
    }

    namespace {

        /// The raw text of the list @p key of a poll; `[]` when it is absent or not a list, as the old document said
        StringView CatalogListText(JsonView params, StringView key, JsonView& list) {
            const Optional<JsonView> member = FindParam(params, key);
            if (member && member->IsArray()) {
                list = *member;
                return member->Raw();
            }
            list = JsonView();
            return "[]";
        }

        /// An input of a workflow: its name, in the order of the bytes of the name, the first of equal names
        struct CatalogInput {
            String name;
            JsonView described;
            size_t order = 0;
        };

    }  // namespace

    void ArtGenSession::ReadCatalog(JsonView params) {
        JsonView presets;
        JsonView workflows;
        const StringView presetsText = CatalogListText(params, "presets", presets);
        const StringView workflowsText = CatalogListText(params, "workflows", workflows);
        // The service sends the lists every poll and they change rarely: the same bytes are not read again. A list that only differs in its
        // formatting is read again, which bumps the version and is harmless
        if (presetsText == StringView(_catalogPresets) && workflowsText == StringView(_catalogWorkflows)) {
            return;
        }
        _catalogPresets.assign(presetsText.data(), presetsText.size());
        _catalogWorkflows.assign(workflowsText.data(), workflowsText.size());
        _status.presets.clear();
        for (const JsonView item : presets.Items()) {
            if (!item.IsObject()) {
                continue;
            }
            ArtGenPreset preset;
            preset.name = StringOr(item, "name");
            preset.title = StringOr(item, "title", StringView(preset.name.data(), preset.name.size()));
            preset.source = StringOr(item, "source", "game");
            preset.kind = StringOr(item, "kind", "image");
            preset.error = StringOr(item, "error");
            if (const Optional<JsonView> data = FindParam(item, "preset"); data && data->IsObject()) {
                preset.data = SortedTextOf(*data);
            }
            if (!preset.name.empty()) {
                _status.presets.push_back(std::move(preset));
            }
        }
        _status.workflows.clear();
        for (const JsonView item : workflows.Items()) {
            if (!item.IsObject()) {
                continue;
            }
            ArtGenWorkflow workflow;
            workflow.name = StringOr(item, "name");
            workflow.source = StringOr(item, "source", "engine");
            workflow.kind = StringOr(item, "kind", "image");
            workflow.error = StringOr(item, "error");
            if (const Optional<JsonView> inputs = FindParam(item, "inputs"); inputs && inputs->IsObject()) {
                // The form lists the inputs in the order of their names, not of the document
                Vector<CatalogInput> ordered;
                ordered.reserve(inputs->Size());
                for (const JsonMember member : inputs->Members()) {
                    CatalogInput& entry = ordered.emplace_back();
                    member.key.ReadText(entry.name);
                    entry.described = member.value;
                    entry.order = ordered.size();
                }
                eastl::sort(ordered.begin(), ordered.end(), [](const CatalogInput& a, const CatalogInput& b) {
                    const int byName = StringView(a.name).compare(StringView(b.name));
                    return byName != 0 ? byName < 0 : a.order < b.order;
                });
                for (size_t i = 0; i < ordered.size(); ++i) {
                    if (i > 0 && ordered[i].name == ordered[i - 1].name) {
                        continue;
                    }
                    ArtGenWorkflowInput input;
                    input.name = ordered[i].name;
                    const JsonView described = ordered[i].described;
                    if (described.IsObject()) {
                        if (const Optional<JsonView> value = FindParam(described, "default")) {
                            input.defaultValue = ArtGenInputValue::Read(*value);
                        }
                        if (const Optional<JsonView> choices = FindParam(described, "choices"); choices && choices->IsArray()) {
                            for (const JsonView choice : choices->Items()) {
                                input.choices.push_back(ArtGenInputValue::Read(choice));
                            }
                        }
                    }
                    workflow.inputs.push_back(std::move(input));
                }
            }
            if (!workflow.name.empty()) {
                _status.workflows.push_back(std::move(workflow));
            }
        }
        ++_version;
    }

    ArtGenRun* ArtGenSession::RunOf(JsonView params) {
        const auto id = GetParamInt(params, "id");
        if (id && _run.state == ArtGenRunState::Running && *id == static_cast<int64_t>(_run.id)) {
            return &_run;
        }
        return nullptr;
    }

    std::expected<ArtGenStaleAnswer, ControlError> ArtGenSession::HandleProgress(JsonView params) {
        ArtGenRun* run = RunOf(params);
        ArtGenStaleAnswer reply;
        reply.stale = run == nullptr;
        if (run != nullptr) {
            run->image = static_cast<int32_t>(GetParamInt(params, "image").value_or(run->image));
            run->step = static_cast<int32_t>(GetParamInt(params, "step").value_or(0));
            run->steps = static_cast<int32_t>(GetParamInt(params, "steps").value_or(0));
            ++_version;
        }
        return reply;
    }

    std::expected<ArtGenStaleAnswer, ControlError> ArtGenSession::HandleImage(JsonView params) {
        ArtGenRun* run = RunOf(params);
        ArtGenStaleAnswer reply;
        reply.stale = run == nullptr;
        if (run == nullptr) {
            return reply;
        }
        ArtGenVariant variant;
        variant.path = StringOr(params, "path");
        variant.provenancePath = StringOr(params, "provenance");
        variant.kind = StringOr(params, "kind", "image");
        const auto seed = GetParamInt(params, "seed");
        if (variant.path.empty() || !seed) {
            return std::unexpected(InvalidParams("'path' (string) and 'seed' (integer) are required"));
        }
        variant.seed = *seed;
        run->variants.push_back(std::move(variant));
        run->image = static_cast<int32_t>(run->variants.size());
        run->step = 0;
        RecordSetImage(run->variants.back());
        ++_version;
        return reply;
    }

    std::expected<ArtGenStaleAnswer, ControlError> ArtGenSession::HandleDone(JsonView params) {
        ArtGenRun* run = RunOf(params);
        ArtGenStaleAnswer reply;
        reply.stale = run == nullptr;
        if (run == nullptr) {
            return reply;
        }
        const String status = StringOr(params, "status");
        if (status == "ok") {
            run->state = ArtGenRunState::Done;
        } else if (status == "cancelled") {
            run->state = ArtGenRunState::Cancelled;
        } else if (status == "failed") {
            run->state = ArtGenRunState::Failed;
            run->error = StringOr(params, "error", "the service gave no reason");
        } else {
            return std::unexpected(InvalidParams("'status' must be \"ok\", \"failed\" or \"cancelled\""));
        }
        run->step = 0;
        ++_version;
        EndSetJob("cancelled");
        PumpSets();
        return reply;
    }

    std::expected<ArtGenQueuedAnswer, ControlError> ArtGenSession::HandleGenerate(JsonView params) {
        const Optional<JsonView> presetJson = FindParam(params, "preset");
        const auto preset = GetParamString(params, "preset");
        const auto subject = GetParamString(params, "subject");
        const bool draft = presetJson && presetJson->IsObject();
        if ((!preset && !draft) || !subject) {
            return std::unexpected(InvalidParams("'preset' must be a name or a preset object and 'subject' a string"));
        }
        const auto count = FindParam(params, "count") ? GetParamInt(params, "count") : Optional<int64_t>(4);
        if (!count) {
            return std::unexpected(InvalidParams("'count' must be an integer"));
        }
        Optional<int64_t> seed;
        if (IsGiven(params, "seed")) {
            seed = GetParamInt(params, "seed");
            if (!seed) {
                return std::unexpected(InvalidParams("'seed' must be an integer"));
            }
        }
        Optional<ArtGenReference> reference;
        if (IsGiven(params, "reference")) {
            auto read = ReadArtGenReference(*FindParam(params, "reference"));
            if (!read) {
                return std::unexpected(InvalidParams(read.error()));
            }
            reference = std::move(*read);
        }
        const int32_t clamped = *count > kMaxCount || *count < 0 ? 0 : static_cast<int32_t>(*count);
        auto id = draft ? RequestDraft(GetParamString(params, "presetName").value_or(String()), SortedTextOf(*presetJson), *subject, clamped, seed, false, std::move(reference))
                        : Request(*preset, *subject, clamped, seed, false, std::move(reference));
        if (!id) {
            const StringView code = IsBusy() ? kBusyCode : ControlErrors::kInvalidParams;
            return std::unexpected(ControlError{String(code), id.error()});
        }
        ArtGenQueuedAnswer reply;
        reply.id = *id;
        reply.serviceConnected = IsServiceConnected();
        return reply;
    }

    std::expected<ArtGenAcceptAnswer, ControlError> ArtGenSession::HandleAccept(JsonView params) {
        const auto seed = GetParamInt(params, "seed");
        if (!seed) {
            return std::unexpected(InvalidParams("'seed' (integer) is required"));
        }
        const Optional<JsonView> use = FindParam(params, "useOnSelected");
        if (use && !use->IsBool()) {
            return std::unexpected(InvalidParams("'useOnSelected' must be a boolean"));
        }
        const bool useOnSelected = use && use->AsBool().value_or(false);
        const ArtGenVariant* variant = FindVariant(*seed);
        if (variant != nullptr && variant->IsAudio()) {
            ArtGenSoundTarget target;
            target.name = GetParamString(params, "name").value_or(String());
            target.clip = GetParamString(params, "clip").value_or(String());
            const Optional<JsonView> soundJson = FindParam(params, "sound");
            auto settings = ReadArtGenSoundSettings(soundJson ? *soundJson : JsonView());
            if (!settings) {
                return std::unexpected(InvalidParams(settings.error()));
            }
            return AcceptSound(*seed, target, *settings, useOnSelected);
        }
        const auto sprite = GetParamString(params, "sprite");
        if (!sprite) {
            return std::unexpected(InvalidParams("'sprite' (string) is required for an image"));
        }
        ArtGenImageSettings settings = ImageSettingsOfRun();
        if (const Optional<JsonView> image = FindParam(params, "image"); image && !image->IsNull()) {
            if (!image->IsObject()) {
                return std::unexpected(InvalidParams("'image' must be an object {trim, padding, maxSize}"));
            }
            // The call's keys over the preset's settings: the settings as a record (`JsonMemberMap`, keys in byte order) take each key of the call
            // as its value, then are read like any record, so a key the call leaves out keeps the preset's and a bad one names itself
            JsonMemberMap merged;
            String base;
            JsonWriter baseWriter(base);
            WriteArtGenImageSettings(baseWriter, settings);
            JsonTape baseTape;
            static_cast<void>(baseTape.Parse(base));
            merged.Assign(JsonView(baseTape, 0), {});
            for (const JsonMember member : image->Members()) {
                String key;
                member.key.ReadText(key);
                merged.Set(key, member.value.Raw());
            }
            String mergedText;
            JsonWriter mergedWriter(mergedText);
            merged.Write(mergedWriter);
            JsonTape mergedTape;
            static_cast<void>(mergedTape.Parse(mergedText));
            auto parsed = ReadArtGenImageSettings(JsonView(mergedTape, 0));
            if (!parsed) {
                return std::unexpected(InvalidParams(Format("'image': {}", parsed.error())));
            }
            settings = *parsed;
        }
        String atlas = AtlasOfRun();
        if (IsGiven(params, "atlas")) {
            const auto name = GetParamString(params, "atlas");
            if (!name || (!name->empty() && !IsValidSpriteName(StringView(*name)))) {
                return std::unexpected(InvalidParams("'atlas' must be an atlas group name (1 to 64 of A-Z a-z 0-9 _ -) or \"\" for none"));
            }
            atlas = *name;
        }
        return Accept(*seed, *sprite, useOnSelected, &settings, &atlas);
    }

    std::expected<ArtGenStaleAnswer, ControlError> ArtGenSession::HandleSetupReport(JsonView params) {
        const auto id = GetParamInt(params, "id");
        const bool current = id && _setup.state == ArtGenSetupState::Running && *id == static_cast<int64_t>(_setup.id);
        ArtGenStaleAnswer reply;
        reply.stale = !current;
        if (!current) {
            return reply;
        }
        const String event = StringOr(params, "event");
        if (event == "plan") {
            _setup.platform = StringOr(params, "platform");
            _setup.platformNote = StringOr(params, "note");
            _setup.root = StringOr(params, "root");
            _setup.downloadBytes = GetParamInt(params, "downloadBytes").value_or(0);
            _setup.plan.clear();
            if (const Optional<JsonView> steps = FindParam(params, "steps"); steps && steps->IsArray()) {
                for (const JsonView item : steps->Items()) {
                    if (!item.IsObject()) {
                        continue;
                    }
                    ArtGenSetupStep step;
                    step.what = StringOr(item, "what");
                    step.dest = StringOr(item, "dest");
                    step.state = StringOr(item, "state");
                    step.licence = StringOr(item, "licence");
                    step.size = GetParamInt(item, "size").value_or(0);
                    _setup.plan.push_back(std::move(step));
                }
            }
        } else if (event == "step") {
            _setup.current = StringOr(params, "what");
            _setup.done = 0;
            _setup.total = 0;
        } else if (event == "progress") {
            _setup.done = GetParamInt(params, "done").value_or(0);
            _setup.total = GetParamInt(params, "total").value_or(0);
        } else if (event == "log") {
            _setup.log = StringOr(params, "text");
        } else if (event == "done") {
            const Optional<JsonView> ok = FindParam(params, "ok");
            if (!ok || !ok->IsBool()) {
                return std::unexpected(InvalidParams("'ok' (boolean) is required with the event done"));
            }
            const bool succeeded = ok->AsBool().value_or(false);
            _setup.state = succeeded ? ArtGenSetupState::Done : ArtGenSetupState::Failed;
            _setup.error = succeeded ? String() : StringOr(params, "error", "the service gave no reason");
            _setup.current.clear();
        } else {
            return std::unexpected(InvalidParams("'event' must be plan, step, progress, log or done"));
        }
        ++_version;
        return reply;
    }

    std::expected<ArtGenSetupQueuedAnswer, ControlError> ArtGenSession::HandleSetup(JsonView params) {
        const auto name = GetParamString(params, "action");
        if (!name) {
            return std::unexpected(InvalidParams("'action' must be a string"));
        }
        if (*name == "cancel") {
            if (!IsSetupBusy()) {
                return std::unexpected(ControlError{String(kNoSetupCode), String("no setup action in work")});
            }
            CancelSetup();
            return ArtGenSetupQueuedAnswer{};
        }
        const auto action = EnumUtils<ArtGenSetupAction>::Cast(StringView(name->data(), name->size()));
        if (!action || *action == ArtGenSetupAction::None) {
            return std::unexpected(InvalidParams("'action' must be Plan, Install, Start, Stop or cancel"));
        }
        auto id = RequestSetup(*action);
        if (!id) {
            return std::unexpected(ControlError{String(kBusyCode), id.error()});
        }
        ArtGenSetupQueuedAnswer reply;
        reply.id = *id;
        reply.serviceConnected = IsServiceConnected();
        return reply;
    }

    std::expected<ArtGenSavedAnswer, ControlError> ArtGenSession::HandleSavePreset(JsonView params) {
        const auto name = GetParamString(params, "name");
        const Optional<JsonView> preset = FindParam(params, "preset");
        if (!name || !preset) {
            return std::unexpected(InvalidParams("'name' (string) and 'preset' (object) are required"));
        }
        auto saved = SavePreset(*name, SortedTextOf(*preset));
        if (!saved) {
            return std::unexpected(InvalidParams(saved.error()));
        }
        ArtGenSavedAnswer reply;
        reply.path = *saved;
        return reply;
    }

    std::expected<ArtGenCancelAnswer, ControlError> ArtGenSession::HandleCancel(JsonView params) {
        if (IsGiven(params, "job") || IsGiven(params, "set") || IsGiven(params, "all")) {
            return CancelFromParams(params);
        }
        if (!IsRunInWork()) {
            return std::unexpected(MakeError(kNoRunCode, String("no run in work")));
        }
        Cancel();
        return ArtGenCancelAnswer{};
    }

    ArtGenStateAnswer ArtGenSession::BuildState() const {
        ArtGenStateAnswer state;
        ArtGenServiceAnswer& service = state.service;
        service.connected = IsServiceConnected();
        service.polls = _pollCount;
        service.state = _status.state;
        service.reason = _status.reason;
        service.generator = _status.generator;
        service.startedHere = _status.startedHere;
        for (const auto& preset : _status.presets) {
            ArtGenPresetRowAnswer row;
            row.name = preset.name;
            row.title = preset.title;
            row.source = preset.source;
            row.kind = preset.kind;
            if (!preset.error.empty()) {
                row.error = preset.error;
            }
            service.presets.push_back(std::move(row));
        }
        for (const auto& workflow : _status.workflows) {
            ArtGenWorkflowRowAnswer row;
            row.name = workflow.name;
            row.source = workflow.source;
            row.kind = workflow.kind;
            if (!workflow.error.empty()) {
                row.error = workflow.error;
            }
            service.workflows.push_back(std::move(row));
        }

        ArtGenRunAnswer& run = state.run;
        run.id = _run.id;
        run.state = String(EnumUtils<ArtGenRunState>::ToString(_run.state));
        run.preset = _run.preset;
        run.draft = !_run.presetData.empty();
        run.subject = _run.subject;
        run.count = _run.count;
        run.seed = _run.seed;
        if (_run.reference) {
            run.reference = MakeArtGenReferenceAnswer(*_run.reference);
        }
        run.image = _run.image;
        run.step = _run.step;
        run.steps = _run.steps;
        run.error = _run.error;
        run.cancelRequested = _run.cancelRequested;
        for (const auto& variant : _run.variants) {
            ArtGenVariantAnswer item;
            item.seed = variant.seed;
            item.path = variant.path;
            item.provenance = variant.provenancePath;
            item.kind = variant.kind;
            run.variants.push_back(std::move(item));
        }

        ArtGenSetupAnswer& setup = state.setup;
        setup.id = _setup.id;
        setup.action = String(EnumUtils<ArtGenSetupAction>::ToString(_setup.action));
        setup.state = String(EnumUtils<ArtGenSetupState>::ToString(_setup.state));
        setup.platform = _setup.platform;
        setup.root = _setup.root;
        setup.downloadBytes = _setup.downloadBytes;
        for (const auto& step : _setup.plan) {
            ArtGenSetupStepAnswer item;
            item.what = step.what;
            item.state = step.state;
            item.size = step.size;
            item.licence = step.licence;
            setup.plan.push_back(std::move(item));
        }
        setup.current = _setup.current;
        setup.done = _setup.done;
        setup.total = _setup.total;
        setup.log = _setup.log;
        setup.error = _setup.error;
        setup.cancelRequested = _setup.cancelRequested;

        state.sets = BuildSetSummaries();
        state.gameRoot = _gameRoot;
        return state;
    }

}  // namespace BECore
