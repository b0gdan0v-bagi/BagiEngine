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
        service.DescribeMethod("artgen_poll", ControlMethodInfo{"Called by the art generation service: its status in, the game root and a new request or a cancel out", {}});
    }

    std::expected<uint32_t, String> ArtGenSession::Request(StringView preset, StringView subject, int32_t count, Optional<int64_t> seed, bool keepVariants) {
        if (IsBusy()) {
            return std::unexpected(String("a run is in work; cancel it or wait"));
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
        const bool changed = status.state != _status.state || status.reason != _status.reason || status.presets.size() != _status.presets.size();
        _status = std::move(status);
        _lastPollSeconds = _clock();
        ++_pollCount;
        if (changed || !wasConnected) {
            ++_version;
        }

        Json reply = Json::object();
        reply["gameRoot"] = ToJsonString(_gameRoot);
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

        Json state = Json::object();
        state["service"] = std::move(service);
        state["run"] = std::move(run);
        state["gameRoot"] = ToJsonString(_gameRoot);
        return state;
    }

}  // namespace BECore
