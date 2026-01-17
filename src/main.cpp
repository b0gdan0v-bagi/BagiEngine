#include <Application/Application.h>
#include <BECore/Application/ApplicationMainAccess.h>

int main(int argc, char* argv[]) {
    if (!BECore::Application::GetInstance().Initialize(BECore::PassKey<BECore::ApplicationMainAccess>{})) {
        return -1;
    }

    BECore::Application::GetInstance().Run(BECore::PassKey<BECore::ApplicationMainAccess>{});

    return 0;
}