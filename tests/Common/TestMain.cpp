#include <Common/EngineEnvironment.h>
#include <Common/Require.h>

namespace {

    /// Destroys the stand-ins of the lookups that failed in a case, once the case and its TearDown are over
    class RequireStandInsCleaner : public ::testing::EmptyTestEventListener {
        void OnTestEnd(const ::testing::TestInfo&) override {
            BECore::Tests::ClearRequireStandIns();
        }
    };

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // gtest takes ownership of the environment and the listener, so these are the places a raw new is required.
    ::testing::AddGlobalTestEnvironment(new BECore::Tests::EngineEnvironment());
    ::testing::UnitTest::GetInstance()->listeners().Append(new RequireStandInsCleaner());

    return RUN_ALL_TESTS();
}
