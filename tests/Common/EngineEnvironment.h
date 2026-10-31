#pragma once

#include <Common/AssertBridge.h>
#include <gtest/gtest.h>

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

    private:
        AssertBridge _assertBridge;
    };

}  // namespace BECore::Tests
