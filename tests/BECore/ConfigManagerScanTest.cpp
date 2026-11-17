#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/PoolString/PoolString.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    class ConfigManagerScanTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(ConfigManagerScanTest, EveryLayerNamesItsFilesWithOneBatch) {
        // A second manager over the same layers as the engine's own: the file stems it finds are the ones already loaded
        const size_t layers = CoreManager::GetFileSystem().GetMountLayers("config"_intern).size();
        ASSERT_GT(layers, 0u);

        ConfigManager scanned;
        const PoolStringStats before = PoolString::GetStats();
        scanned.Initialize();
        const PoolStringStats after = PoolString::GetStats();

        EXPECT_EQ(after.batchCalls - before.batchCalls, layers);
        for (const char* name : {"LoggerConfig", "TextConfig", "SceneConfig"}) {
            const PoolString key = PoolString::Intern(StringView(name));
            EXPECT_FALSE(scanned.GetConfig(key).IsEmpty()) << name;
        }
    }

}  // namespace BECore::Tests
