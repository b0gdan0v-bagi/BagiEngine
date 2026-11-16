#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/Json.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Utils/Singleton.h>
#include <cstdint>
#include <expected>

namespace BECore {

    class ControlService;
    class SceneNode;

    CORE_ENUM(ArtGenRunState, uint8_t, Idle, Queued, Running, Done, Failed, Cancelled)

    /// A style preset of the game as the service reported it; a broken one carries its error and cannot be picked
    struct ArtGenPreset {
        String name;
        String title;
        String error;
    };

    /// What the generation service last said about itself in a poll
    struct ArtGenServiceStatus {
        String state;  // "ready", "not_running", "not_installed" (tools/artgen/CLAUDE.md, "Generators")
        String reason;
        String generator;
        Vector<ArtGenPreset> presets;
    };

    struct ArtGenVariant {
        int64_t seed = 0;
        String path;  // real path of the PNG the service wrote
        String provenancePath;
    };

    /// One request and what came back for it
    struct ArtGenRun {
        uint32_t id = 0;
        ArtGenRunState state = ArtGenRunState::Idle;
        String preset;
        String subject;
        int32_t count = 0;
        Optional<int64_t> seed;
        int32_t image = 0;  // the image in work, 0-based
        int32_t step = 0;
        int32_t steps = 0;
        String error;
        Vector<ArtGenVariant> variants;
        int32_t keptVariants = 0;  // the variants of the last run it started with (Request with keepVariants)
        bool cancelRequested = false;
    };

    /**
     * @brief The editor's side of the art generation contract: the request the Generate panel (or a client) makes, and what the generation
     * service (tools/artgen/artgen_bridge.py, a control API client) reports back. No ImGui: the panel draws it.
     *
     * The service pulls (src/Widgets/ArtGen/CLAUDE.md, "The contract"): it calls `artgen_poll` with its status about twice a second and
     * gets the game root, a new request once and a cancel once; it reports with `artgen_progress`, `artgen_image` and `artgen_done`. One run at
     * a time. It lives as long as the program (the control API cannot drop a handler, and the panel is re-created at a switch of the run mode),
     * so the handlers capture the session, never a widget. Main thread only, like the control API.
     */
    class ArtGenSession : public Singleton<ArtGenSession> {
    public:
        /// Seconds without a poll after which the service counts as gone
        static constexpr double kServiceTimeoutSeconds = 3.0;
        static constexpr int32_t kMaxCount = 16;

        using Clock = MoveOnlyFunction<double()>;
        /// Accepts a variant as the sprite @p sprite and, when @p node is given, puts it on that node; the answer of `artgen_accept`
        using Acceptor = std::expected<Json, ControlError> (*)(const ArtGenVariant& variant, StringView sprite, SceneNode* node);

        /// @param clock monotonic seconds; the default is the steady clock (a test passes its own). The editor's one session is GetInstance()
        explicit ArtGenSession(Clock clock = {});
        ~ArtGenSession() override;

        /// Registers the methods on @p service (once per service; a second call with the same service does nothing)
        void RegisterControl(ControlService& service);

        /// The real path of the game the service works for (where its presets are, `artgen/presets/`)
        void SetGameRoot(StringView root) {
            _gameRoot = String(root);
        }

        /// Queues a request; the error says why not (a run in work, a bad count or preset). @p keepVariants starts the new run with the
        /// variants of the last one, so a few more can be made without losing the ones on screen
        std::expected<uint32_t, String> Request(StringView preset, StringView subject, int32_t count, Optional<int64_t> seed, bool keepVariants = false);
        /// Asks the service to stop the run; the images already made stay
        void Cancel();

        /// What `artgen_accept` and the panel's Accept run (ArtGenAccept.h in the editor; a fake in the tests); without one `artgen_accept` fails
        void SetAcceptor(Acceptor acceptor) {
            _acceptor = acceptor;
        }
        /// The node "Use on selected" puts the sprite on (the scene editor's primary selection); null for none. Held, so it is checked alive
        /// in the active scene before use
        void SetSelectedNode(IntrusivePtr<SceneNode> node);
        /// The selected node when it is still in the tree of the active scene, else null
        [[nodiscard]] SceneNode* GetSelectedNodeIfAlive() const;
        /// The variant of the current run with @p seed, or null
        [[nodiscard]] const ArtGenVariant* FindVariant(int64_t seed) const;
        /// Accepts the variant @p seed as @p sprite, and puts it on the selected node when @p useOnSelected
        std::expected<Json, ControlError> Accept(int64_t seed, StringView sprite, bool useOnSelected);

        [[nodiscard]] bool IsServiceConnected() const;
        [[nodiscard]] const ArtGenServiceStatus& GetServiceStatus() const {
            return _status;
        }
        [[nodiscard]] const ArtGenRun& GetRun() const {
            return _run;
        }
        [[nodiscard]] bool IsBusy() const {
            return _run.state == ArtGenRunState::Queued || _run.state == ArtGenRunState::Running;
        }
        /// Grows on every change the panel shows (a poll that changed the status, a step, an image, the end)
        [[nodiscard]] uint64_t Version() const {
            return _version;
        }
        [[nodiscard]] uint64_t PollCount() const {
            return _pollCount;
        }

        /// The handlers, public for the tests: each takes the params of the call and answers like the method
        std::expected<Json, ControlError> HandlePoll(const Json& params);
        std::expected<Json, ControlError> HandleProgress(const Json& params);
        std::expected<Json, ControlError> HandleImage(const Json& params);
        std::expected<Json, ControlError> HandleDone(const Json& params);
        std::expected<Json, ControlError> HandleGenerate(const Json& params);
        std::expected<Json, ControlError> HandleCancel(const Json& params);
        std::expected<Json, ControlError> HandleAccept(const Json& params);
        [[nodiscard]] Json StateJson() const;

    private:
        /// The run a report of the service is about, or null when it is about an older one (a late message after a new request)
        ArtGenRun* RunOf(const Json& params);

        Clock _clock;
        Acceptor _acceptor = nullptr;
        IntrusivePtr<SceneNode> _selectedNode;
        String _gameRoot;
        ArtGenServiceStatus _status;
        ArtGenRun _run;
        uint32_t _nextId = 1;
        double _lastPollSeconds = -1.0;
        uint64_t _pollCount = 0;
        uint64_t _version = 0;
        const ControlService* _registeredOn = nullptr;
    };

}  // namespace BECore
