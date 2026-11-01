#pragma once

namespace BECore {

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
        void Cleanup();

        /// Starts the WebSocket server of the control API on @p port. False, with the reason in the log, when it cannot listen
        bool StartControlServer(uint16_t port);

        bool _isRunning = false;
        eastl::vector<eastl::string> _arguments;
        eastl::unique_ptr<WebSocketServer> _controlServer;
    };
}  // namespace BECore
