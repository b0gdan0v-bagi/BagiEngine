#pragma once

namespace BECore {

    // Forward declaration для класса-ключа доступа
    class ApplicationMainAccess;

    class Application : public Singleton<Application>, public SubscriptionHolder {
    public:
        Application() = default;
        ~Application() override = default;

        bool Initialize();
        void Run() const;

    private:

        void StopApplication();
        static void Cleanup();

        bool _isRunning = false;
    };
}  // namespace BECore
