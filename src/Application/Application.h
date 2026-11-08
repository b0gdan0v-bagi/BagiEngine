#pragma once

namespace BECore {

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    // Forward declaration для класса-ключа доступа
    class ApplicationMainAccess;
    class WebSocketServer;

    class Application : public Singleton<Application>, public SubscriptionHolder {
    public:
        Application();
        ~Application() override;

        /// Stores the command line without the program name. Call it before Initialize()
        void SetArguments(int argc, const char* const* argv);

        bool Initialize();
        void Run();

    private:
        void StopApplication();

#if BE_WITH_EDITOR
        /// Ctrl+Shift+E (IsEditorToggle) asks for the other run mode; the switch itself waits for the end of the frame (Run).
        /// Read from the raw SDL event, which is emitted before the ImGui capture check, so the chord works over a text field too.
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const;
#endif
        void Cleanup();

        /// Starts the WebSocket server of the control API on @p port. False, with the reason in the log, when it cannot listen
        bool StartControlServer(uint16_t port);

        bool _isRunning = false;
        bool _headless = false;  // there is no window to show an editor in: the switch chord is ignored
        eastl::vector<eastl::string> _arguments;
        eastl::unique_ptr<WebSocketServer> _controlServer;
    };
}  // namespace BECore
