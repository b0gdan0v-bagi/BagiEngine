#include <Application/Application.h>

int main(int argc, char* argv[]) {
    if (!Core::Application::GetInstance().Initialize()) {
        return -1;
    }

    Core::Application::GetInstance().Run();
    Core::Application::GetInstance().Cleanup();

    return 0;
}