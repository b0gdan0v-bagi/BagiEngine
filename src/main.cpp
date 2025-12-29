#include <Application/Application.h>
#include <Application/ApplicationMainAccess.h>

int main(int argc, char* argv[]) {
    if (!Core::Application::GetInstance().Initialize(Core::PassKey<Core::ApplicationMainAccess>{})) {
        return -1;
    }

    Core::Application::GetInstance().Run(Core::PassKey<Core::ApplicationMainAccess>{});
    Core::Application::GetInstance().Cleanup(Core::PassKey<Core::ApplicationMainAccess>{});

    return 0;
}