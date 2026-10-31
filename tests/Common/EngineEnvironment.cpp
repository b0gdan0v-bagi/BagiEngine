#include "EngineEnvironment.h"

#include <BECore/GameManager/CoreManager.h>

namespace BECore::Tests {

    namespace {

        bool g_coreServicesUp = false;

    }  // namespace

    void EngineEnvironment::SetUp() {
        _assertBridge.Install();
    }

    void EngineEnvironment::TearDown() {
        if (g_coreServicesUp) {
            CoreManager::GetInstance().OnTestEnvironmentDeinit(PassKey<EngineEnvironment>{});
            g_coreServicesUp = false;
        }
    }

    void EngineEnvironment::EnsureCoreServices() {
        if (g_coreServicesUp) {
            return;
        }
        CoreManager::GetInstance().OnTestEnvironmentInit(PassKey<EngineEnvironment>{});
        g_coreServicesUp = true;
    }

}  // namespace BECore::Tests
