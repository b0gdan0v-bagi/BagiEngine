#include <Common/EngineEnvironment.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // gtest takes ownership of the environment, so this is the one place a raw new is required.
    ::testing::AddGlobalTestEnvironment(new BECore::Tests::EngineEnvironment());

    return RUN_ALL_TESTS();
}
