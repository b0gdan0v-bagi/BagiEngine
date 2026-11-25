#pragma once

#include <BECore/Math/Color.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>
#include <Events/SubscriptionHolder.h>

#if BE_WITH_REMOTE_CONTROL
#include <cstdint>
#endif

namespace BECore {

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    class ApplicationMainAccess;
    struct CookRequest;
#if BE_WITH_REMOTE_CONTROL
    class WebSocketServer;
#endif

    class Application : public Singleton<Application>, public SubscriptionHolder {
    public:
        Application();
        ~Application() override;

        /// Stores the command line without the program name. Call it before Initialize()
        void SetArguments(int argc, const char* const* argv);

        /// `--cook=<dir>` (BECore/Cook/CLAUDE.md): cooks, and verifies when asked, the content of the roots added to the file system, with the file
        /// system and the task manager only: no window, GPU or control server, and no `Initialize()`. Returns the exit code of the process
        int RunCook(const CookRequest& request);

        bool Initialize();
        /// The whole run: RunFrame() until it returns false, then Finish(). What a desktop executable calls
        void Run();
        /// One iteration of the main loop (the manual-clock idle pass included). False once the application has been asked to stop.
        /// A host that owns the loop (the browser's frame callback) calls it itself, then Finish()
        bool RunFrame();
        /// Shuts everything down after the last frame (the work that Run() does after the loop). Call it once (checked by an ASSERT)
        void Finish();

        /// The exit code of the process (0 unless something set it): `--replay=` leaves 1 when the replay did not play to its end
        void SetExitCode(int code) {
            _exitCode = code;
        }

        [[nodiscard]] int GetExitCode() const {
            return _exitCode;
        }

    private:
        void StopApplication();

        /// Posts the control method `replay` for @p path; the answer sets the exit code and, with @p quitWhenDone, stops the application
        void PostReplay(const String& path, bool quitWhenDone);

#if BE_WITH_EDITOR
        /// Ctrl+Shift+E (IsEditorToggle) asks for the other run mode; the switch itself waits for the end of the frame (RunFrame).
        /// Read from the raw SDL event, which is emitted before the ImGui capture check, so the chord works over a text field too.
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const;
#endif
        void Cleanup();

        /// Sets the window title when the active game or the run mode differs from the ones of the last call (a compare per frame)
        void UpdateWindowTitle();

        /// Copies the state of the control server into ControlService (every frame, a few words), so that the editor can show it
        void PublishControlStatus() const;

#if BE_WITH_REMOTE_CONTROL
        /// Starts the WebSocket server of the control API on @p port. False, with the reason in the log, when it cannot listen
        bool StartControlServer(uint16_t port);
        /// StartControlServerRequestEvent: the default port, else a free one; the reason of a failure goes to the log
        void OnStartControlServerRequest();
#endif

        bool _isRunning = false;
        bool _finished = false;  // Finish() has run: it must run once
        int _exitCode = 0;
        bool _headless = false;               // a headless window gets no keys: the switch chord is ignored
        Color _clearColor{20, 20, 100, 255};  // ApplicationConfig::_clearColor: what IRenderer::BeginFrame() opens each frame with
        // The key of the title last applied: the active game's name and the editor mode
        PoolString _titleGame;
        bool _titleEditor = false;
        bool _titleApplied = false;
        Vector<String> _arguments;
#if BE_WITH_REMOTE_CONTROL
        UniquePtr<WebSocketServer> _controlServer;
#endif
    };
}  // namespace BECore
