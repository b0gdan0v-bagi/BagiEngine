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
#ifdef BAGI_ENGINE_ROOT
        // The working directory is inside whichever repository builds the tests: pin the engine's config/ and assets/
        CoreManager::GetFileSystem().AddContentRoot(BAGI_ENGINE_ROOT);
#endif
        CoreManager::GetInstance().OnTestEnvironmentInit(PassKey<EngineEnvironment>{});
        g_coreServicesUp = true;
    }

    void EngineEnvironment::UseSceneManager(IntrusivePtrAtomic<SceneManager> manager) {
        CoreManager::GetInstance().SetSceneManagerForTest(std::move(manager), PassKey<EngineEnvironment>{});
    }

}  // namespace BECore::Tests
