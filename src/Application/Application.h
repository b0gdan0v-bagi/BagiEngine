#pragma once

namespace BECore {

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    // Forward declaration для класса-ключа доступа
    class ApplicationMainAccess;
#if BE_WITH_REMOTE_CONTROL
    class WebSocketServer;
#endif

    class Application : public Singleton<Application>, public SubscriptionHolder {
    public:
        Application();
        ~Application() override;

        /// Stores the command line without the program name. Call it before Initialize()
        void SetArguments(int argc, const char* const* argv);

        bool Initialize();
        /// The whole run: RunFrame() until it returns false, then Finish(). What a desktop executable calls
        void Run();
        /// One iteration of the main loop (the manual-clock idle pass included). False once the application has been asked to stop.
        /// A host that owns the loop (the browser's frame callback) calls it itself, then Finish()
        bool RunFrame();
        /// Shuts everything down after the last frame (the work that Run() does after the loop). Call it once (checked by an ASSERT)
        void Finish();

    private:
        void StopApplication();

#if BE_WITH_EDITOR
        /// Ctrl+Shift+E (IsEditorToggle) asks for the other run mode; the switch itself waits for the end of the frame (RunFrame).
        /// Read from the raw SDL event, which is emitted before the ImGui capture check, so the chord works over a text field too.
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const;
#endif
        void Cleanup();

#if BE_WITH_REMOTE_CONTROL
        /// Starts the WebSocket server of the control API on @p port. False, with the reason in the log, when it cannot listen
        bool StartControlServer(uint16_t port);
#endif

        bool _isRunning = false;
        bool _finished = false;  // Finish() has run: it must run once
        bool _headless = false;  // there is no window to show an editor in: the switch chord is ignored
        eastl::vector<eastl::string> _arguments;
#if BE_WITH_REMOTE_CONTROL
        eastl::unique_ptr<WebSocketServer> _controlServer;
#endif
    };
}  // namespace BECore
