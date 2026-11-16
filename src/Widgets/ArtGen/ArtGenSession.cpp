#include "ArtGenSession.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <chrono>

namespace BECore {

    namespace {

        constexpr StringView kBusyCode = "artgen_busy";
        constexpr StringView kNoRunCode = "artgen_no_run";
        constexpr StringView kNoVariantCode = "artgen_no_variant";
        constexpr StringView kAcceptFailedCode = "artgen_accept_failed";
        constexpr StringView kNoSetupCode = "artgen_no_setup";

        double SteadySeconds() {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            return std::chrono::duration<double>(now).count();
        }

        ControlError InvalidParams(StringView message) {
            return ControlError{String(ControlErrors::kInvalidParams), String(message)};
        }

        String StringOr(const Json& object, StringView key, StringView fallback = {}) {
            const auto value = GetJsonString(object, key);
            return value ? *value : String(fallback);
        }

        Json ToJsonString(const String& text) {
            return MakeJsonString(StringView(text.data(), text.size()));
        }

        /// Wraps a handler of the session as a control method: a value is the reply, an error the failure
        template <auto Method>
        void Register(ControlService& service, ArtGenSession& session, StringView name) {
            service.RegisterHandler(name, [&session](ControlCall& call) {
                auto result = (session.*Method)(call.Params());
                if (result) {
                    call.Reply(std::move(*result));
                } else {
                    call.Fail(result.error());
                }
            });
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

    std::expected<Json, ControlError> ArtGenSession::Accept(int64_t seed, StringView sprite, bool useOnSelected) {
        const ArtGenVariant* variant = FindVariant(seed);
        if (variant == nullptr) {
            return std::unexpected(ControlError{String(kNoVariantCode), Format("the current run has no image of seed {}", seed)});
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
        return _acceptor(*variant, sprite, node);
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
        service.RegisterHandler("artgen_state", [this](ControlCall& call) { call.Reply(StateJson()); });

        service.DescribeMethod("artgen_generate", ControlMethodInfo{"Asks the art generation service for images of a style preset of the game (the Generate panel's button)",
                                                                    {ControlParamInfo{"preset", "string", true, "a preset name of the game (artgen/presets/<name>.json)"},
                                                                     ControlParamInfo{"subject", "string", true, "what to draw; replaces {subject} in the preset's prompt"},
                                                                     ControlParamInfo{"count", "integer", false, "images, 1 to 16 (default 4)"},
                                                                     ControlParamInfo{"seed", "integer", false, "the first seed; image i takes seed + i (default random)"}}});
        service.DescribeMethod("artgen_accept", ControlMethodInfo{"Makes an image of the current run a sprite of the game: PNG into assets/artgen, TextureLibrary entry, reload",
                                                                  {ControlParamInfo{"seed", "integer", true, "the seed of the image (artgen_state.run.variants)"},
                                                                   ControlParamInfo{"sprite", "string", true, "the sprite name: 1 to 64 of A-Z a-z 0-9 _ -"},
                                                                   ControlParamInfo{"useOnSelected", "boolean", false, "also put it on the node selected in the scene editor"}}});
        service.DescribeMethod("artgen_cancel", ControlMethodInfo{"Stops the run of the art generation service; the images made stay", {}});
        service.DescribeMethod("artgen_state", ControlMethodInfo{"The art generation service as the editor sees it, and the current run with its images", {}});
        service.DescribeMethod("artgen_setup", ControlMethodInfo{"Asks the art generation service to plan, install, start or stop its generator (the panel's Set up / Start / Stop)",
                                                                 {ControlParamInfo{"action", "string", true, "Plan, Install, Start, Stop, or cancel (an install in work)"}}});
        service.DescribeMethod("artgen_poll", ControlMethodInfo{"Called by the art generation service: its status in, the game root and a new request or a cancel out", {}});
    }

    std::expected<uint32_t, String> ArtGenSession::Request(StringView preset, StringView subject, int32_t count, Optional<int64_t> seed, bool keepVariants) {
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
        for (const auto& known : _status.presets) {
            if (StringView(known.name) == preset && !known.error.empty()) {
                return std::unexpected(Format("the preset '{}' is broken: {}", preset, known.error));
            }
        }
        Vector<ArtGenVariant> kept;
        if (keepVariants) {
            kept = std::move(_run.variants);
        }
        _run = ArtGenRun{};
        _run.variants = std::move(kept);
        _run.keptVariants = static_cast<int32_t>(_run.variants.size());
        _run.id = _nextId++;
        _run.state = ArtGenRunState::Queued;
        _run.preset = String(preset);
        _run.subject = String(subject);
        _run.count = count;
        _run.seed = seed;
        ++_version;
        return _run.id;
    }

    void ArtGenSession::Cancel() {
        if (_run.state == ArtGenRunState::Queued) {
            // The service never saw it: nothing to stop there
            _run.state = ArtGenRunState::Cancelled;
            ++_version;
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

    std::expected<Json, ControlError> ArtGenSession::HandlePoll(const Json& params) {
        if (!params.is_object()) {
            return std::unexpected(InvalidParams("params must be an object"));
        }
        ArtGenServiceStatus status;
        status.state = StringOr(params, "state", "ready");
        status.reason = StringOr(params, "reason");
        status.generator = StringOr(params, "generator");
        status.startedHere = GetJsonBool(params, "startedHere").value_or(false);
        if (const Json* presets = FindJsonMember(params, "presets"); presets != nullptr && presets->is_array()) {
            for (const Json& item : *presets) {
                if (!item.is_object()) {
                    continue;
                }
                ArtGenPreset preset;
                preset.name = StringOr(item, "name");
                preset.title = StringOr(item, "title", StringView(preset.name.data(), preset.name.size()));
                preset.error = StringOr(item, "error");
                if (!preset.name.empty()) {
                    status.presets.push_back(std::move(preset));
                }
            }
        }
        const bool wasConnected = IsServiceConnected();
        const bool changed = status.state != _status.state || status.reason != _status.reason || status.presets.size() != _status.presets.size() || status.startedHere != _status.startedHere;
        _status = std::move(status);
        _lastPollSeconds = _clock();
        ++_pollCount;
        if (changed || !wasConnected) {
            ++_version;
        }

        Json reply = Json::object();
        reply["gameRoot"] = ToJsonString(_gameRoot);
        if (_stopService) {
            // The service exits on this answer and reports nothing more: what it was doing ends here, and it is gone at once
            _stopService = false;
            reply["shutdown"] = true;
            if (_run.state == ArtGenRunState::Running) {
                _run.state = ArtGenRunState::Cancelled;
                _run.step = 0;
            }
            if (_setup.state == ArtGenSetupState::Running) {
                _setup.state = ArtGenSetupState::Failed;
                _setup.error = "the service was stopped";
            }
            _lastPollSeconds = -1.0;
            ++_version;
            return reply;
        }
        if (_run.state == ArtGenRunState::Queued) {
            Json request = Json::object();
            request["id"] = _run.id;
            request["preset"] = ToJsonString(_run.preset);
            request["subject"] = ToJsonString(_run.subject);
            request["count"] = _run.count;
            request["seed"] = _run.seed ? Json(*_run.seed) : Json();
            reply["request"] = std::move(request);
            _run.state = ArtGenRunState::Running;
            ++_version;
        } else if (_run.state == ArtGenRunState::Running && _run.cancelRequested) {
            reply["cancel"] = _run.id;
        }
        if (_setup.state == ArtGenSetupState::Queued) {
            Json setup = Json::object();
            setup["id"] = _setup.id;
            setup["action"] = ToStdString(EnumUtils<ArtGenSetupAction>::ToString(_setup.action));
            reply["setup"] = std::move(setup);
            _setup.state = ArtGenSetupState::Running;
            ++_version;
        } else if (_setup.state == ArtGenSetupState::Running && _setup.cancelRequested) {
            reply["setupCancel"] = _setup.id;
        }
        return reply;
    }

    ArtGenRun* ArtGenSession::RunOf(const Json& params) {
        const auto id = GetJsonInt(params, "id");
        if (id && _run.state == ArtGenRunState::Running && *id == static_cast<int64_t>(_run.id)) {
            return &_run;
        }
        return nullptr;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleProgress(const Json& params) {
        Json reply = Json::object();
        ArtGenRun* run = RunOf(params);
        reply["stale"] = run == nullptr;
        if (run != nullptr) {
            run->image = static_cast<int32_t>(GetJsonInt(params, "image").value_or(run->image));
            run->step = static_cast<int32_t>(GetJsonInt(params, "step").value_or(0));
            run->steps = static_cast<int32_t>(GetJsonInt(params, "steps").value_or(0));
            ++_version;
        }
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleImage(const Json& params) {
        Json reply = Json::object();
        ArtGenRun* run = RunOf(params);
        reply["stale"] = run == nullptr;
        if (run == nullptr) {
            return reply;
        }
        ArtGenVariant variant;
        variant.path = StringOr(params, "path");
        variant.provenancePath = StringOr(params, "provenance");
        const auto seed = GetJsonInt(params, "seed");
        if (variant.path.empty() || !seed) {
            return std::unexpected(InvalidParams("'path' (string) and 'seed' (integer) are required"));
        }
        variant.seed = *seed;
        run->variants.push_back(std::move(variant));
        run->image = static_cast<int32_t>(run->variants.size());
        run->step = 0;
        ++_version;
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleDone(const Json& params) {
        Json reply = Json::object();
        ArtGenRun* run = RunOf(params);
        reply["stale"] = run == nullptr;
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
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleGenerate(const Json& params) {
        const auto preset = GetJsonString(params, "preset");
        const auto subject = GetJsonString(params, "subject");
        if (!preset || !subject) {
            return std::unexpected(InvalidParams("'preset' and 'subject' must be strings"));
        }
        const Json* countJson = FindJsonMember(params, "count");
        const auto count = countJson != nullptr ? GetJsonInt(params, "count") : Optional<int64_t>(4);
        if (!count) {
            return std::unexpected(InvalidParams("'count' must be an integer"));
        }
        Optional<int64_t> seed;
        if (const Json* seedJson = FindJsonMember(params, "seed"); seedJson != nullptr && !seedJson->is_null()) {
            seed = GetJsonInt(params, "seed");
            if (!seed) {
                return std::unexpected(InvalidParams("'seed' must be an integer"));
            }
        }
        const int32_t clamped = *count > kMaxCount || *count < 0 ? 0 : static_cast<int32_t>(*count);
        auto id = Request(*preset, *subject, clamped, seed);
        if (!id) {
            const StringView code = IsBusy() ? kBusyCode : ControlErrors::kInvalidParams;
            return std::unexpected(ControlError{String(code), id.error()});
        }
        Json reply = Json::object();
        reply["id"] = *id;
        reply["serviceConnected"] = IsServiceConnected();
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleAccept(const Json& params) {
        const auto seed = GetJsonInt(params, "seed");
        const auto sprite = GetJsonString(params, "sprite");
        if (!seed || !sprite) {
            return std::unexpected(InvalidParams("'seed' (integer) and 'sprite' (string) are required"));
        }
        const Json* use = FindJsonMember(params, "useOnSelected");
        if (use != nullptr && !use->is_boolean()) {
            return std::unexpected(InvalidParams("'useOnSelected' must be a boolean"));
        }
        return Accept(*seed, *sprite, use != nullptr && use->get<bool>());
    }

    std::expected<Json, ControlError> ArtGenSession::HandleSetupReport(const Json& params) {
        Json reply = Json::object();
        const auto id = GetJsonInt(params, "id");
        const bool current = id && _setup.state == ArtGenSetupState::Running && *id == static_cast<int64_t>(_setup.id);
        reply["stale"] = !current;
        if (!current) {
            return reply;
        }
        const String event = StringOr(params, "event");
        if (event == "plan") {
            _setup.platform = StringOr(params, "platform");
            _setup.platformNote = StringOr(params, "note");
            _setup.root = StringOr(params, "root");
            _setup.downloadBytes = GetJsonInt(params, "downloadBytes").value_or(0);
            _setup.plan.clear();
            if (const Json* steps = FindJsonMember(params, "steps"); steps != nullptr && steps->is_array()) {
                for (const Json& item : *steps) {
                    if (!item.is_object()) {
                        continue;
                    }
                    ArtGenSetupStep step;
                    step.what = StringOr(item, "what");
                    step.dest = StringOr(item, "dest");
                    step.state = StringOr(item, "state");
                    step.licence = StringOr(item, "licence");
                    step.size = GetJsonInt(item, "size").value_or(0);
                    _setup.plan.push_back(std::move(step));
                }
            }
        } else if (event == "step") {
            _setup.current = StringOr(params, "what");
            _setup.done = 0;
            _setup.total = 0;
        } else if (event == "progress") {
            _setup.done = GetJsonInt(params, "done").value_or(0);
            _setup.total = GetJsonInt(params, "total").value_or(0);
        } else if (event == "log") {
            _setup.log = StringOr(params, "text");
        } else if (event == "done") {
            const Json* ok = FindJsonMember(params, "ok");
            if (ok == nullptr || !ok->is_boolean()) {
                return std::unexpected(InvalidParams("'ok' (boolean) is required with the event done"));
            }
            const bool succeeded = ok->get<bool>();
            _setup.state = succeeded ? ArtGenSetupState::Done : ArtGenSetupState::Failed;
            _setup.error = succeeded ? String() : StringOr(params, "error", "the service gave no reason");
            _setup.current.clear();
        } else {
            return std::unexpected(InvalidParams("'event' must be plan, step, progress, log or done"));
        }
        ++_version;
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleSetup(const Json& params) {
        const auto name = GetJsonString(params, "action");
        if (!name) {
            return std::unexpected(InvalidParams("'action' must be a string"));
        }
        if (*name == "cancel") {
            if (!IsSetupBusy()) {
                return std::unexpected(ControlError{String(kNoSetupCode), String("no setup action in work")});
            }
            CancelSetup();
            return Json::object();
        }
        const auto action = EnumUtils<ArtGenSetupAction>::Cast(StringView(name->data(), name->size()));
        if (!action || *action == ArtGenSetupAction::None) {
            return std::unexpected(InvalidParams("'action' must be Plan, Install, Start, Stop or cancel"));
        }
        auto id = RequestSetup(*action);
        if (!id) {
            return std::unexpected(ControlError{String(kBusyCode), id.error()});
        }
        Json reply = Json::object();
        reply["id"] = *id;
        reply["serviceConnected"] = IsServiceConnected();
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleCancel(const Json& /*params*/) {
        if (!IsBusy()) {
            return std::unexpected(ControlError{String(kNoRunCode), String("no run in work")});
        }
        Cancel();
        return Json::object();
    }

    Json ArtGenSession::StateJson() const {
        Json service = Json::object();
        service["connected"] = IsServiceConnected();
        service["polls"] = _pollCount;
        service["state"] = ToJsonString(_status.state);
        service["reason"] = ToJsonString(_status.reason);
        service["generator"] = ToJsonString(_status.generator);
        service["startedHere"] = _status.startedHere;
        Json presets = Json::array();
        for (const auto& preset : _status.presets) {
            Json item = Json::object();
            item["name"] = ToJsonString(preset.name);
            item["title"] = ToJsonString(preset.title);
            if (!preset.error.empty()) {
                item["error"] = ToJsonString(preset.error);
            }
            presets.push_back(std::move(item));
        }
        service["presets"] = std::move(presets);

        Json run = Json::object();
        run["id"] = _run.id;
        run["state"] = ToStdString(EnumUtils<ArtGenRunState>::ToString(_run.state));
        run["preset"] = ToJsonString(_run.preset);
        run["subject"] = ToJsonString(_run.subject);
        run["count"] = _run.count;
        run["seed"] = _run.seed ? Json(*_run.seed) : Json();
        run["image"] = _run.image;
        run["step"] = _run.step;
        run["steps"] = _run.steps;
        run["error"] = ToJsonString(_run.error);
        run["cancelRequested"] = _run.cancelRequested;
        Json variants = Json::array();
        for (const auto& variant : _run.variants) {
            Json item = Json::object();
            item["seed"] = variant.seed;
            item["path"] = ToJsonString(variant.path);
            item["provenance"] = ToJsonString(variant.provenancePath);
            variants.push_back(std::move(item));
        }
        run["variants"] = std::move(variants);

        Json setup = Json::object();
        setup["id"] = _setup.id;
        setup["action"] = ToStdString(EnumUtils<ArtGenSetupAction>::ToString(_setup.action));
        setup["state"] = ToStdString(EnumUtils<ArtGenSetupState>::ToString(_setup.state));
        setup["platform"] = ToJsonString(_setup.platform);
        setup["root"] = ToJsonString(_setup.root);
        setup["downloadBytes"] = _setup.downloadBytes;
        Json plan = Json::array();
        for (const auto& step : _setup.plan) {
            Json item = Json::object();
            item["what"] = ToJsonString(step.what);
            item["state"] = ToJsonString(step.state);
            item["size"] = step.size;
            item["licence"] = ToJsonString(step.licence);
            plan.push_back(std::move(item));
        }
        setup["plan"] = std::move(plan);
        setup["current"] = ToJsonString(_setup.current);
        setup["done"] = _setup.done;
        setup["total"] = _setup.total;
        setup["log"] = ToJsonString(_setup.log);
        setup["error"] = ToJsonString(_setup.error);
        setup["cancelRequested"] = _setup.cancelRequested;

        Json state = Json::object();
        state["service"] = std::move(service);
        state["run"] = std::move(run);
        state["setup"] = std::move(setup);
        state["gameRoot"] = ToJsonString(_gameRoot);
        return state;
    }

}  // namespace BECore
