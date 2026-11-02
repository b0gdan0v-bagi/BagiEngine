#pragma once

#include <Common/AssertBridge.h>
#include <gtest/gtest.h>

namespace BECore {

    class SceneManager;

}  // namespace BECore

namespace BECore::Tests {

    /**
     * @brief Global gtest environment of the engine test executable.
     *
     * SetUp() routes engine assertion failures to gtest (AssertBridge). The engine services themselves
     * (FileSystem, TaskManager, ConfigManager, Logger, ResourceManager) are brought up lazily by
     * EnsureCoreServices(), so pure-logic tests do not read config/ or write engine.log.
     */
    class EngineEnvironment : public ::testing::Environment {
    public:
        void SetUp() override;
        void TearDown() override;

        /// Brings the engine core services up on the first call; later calls do nothing.
        /// Needs the working directory to be inside the repository (FileSystem looks for config/ upwards from it).
        static void EnsureCoreServices();

        /// Makes @p manager the scene manager of CoreManager::GetSceneManager() until it is called again, for a test of a component that switches scenes. Call it with an empty
        /// pointer in TearDown, before the manager is released.
        static void UseSceneManager(IntrusivePtrAtomic<SceneManager> manager);

    private:
        AssertBridge _assertBridge;
    };

}  // namespace BECore::Tests
