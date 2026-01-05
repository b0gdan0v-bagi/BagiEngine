#pragma once

namespace Core {

    // Forward declaration для класса-ключа доступа
    class ApplicationMainAccess;

    class Application : public Singleton<Application> {
    public:
        Application() = default;
        ~Application() override = default;

        bool Initialize(PassKey<ApplicationMainAccess>);
        void Run(PassKey<ApplicationMainAccess>) const;

    private:

        void StopApplication();
        static void Cleanup();

        bool _isRunning = false;
    };
}  // namespace Core
