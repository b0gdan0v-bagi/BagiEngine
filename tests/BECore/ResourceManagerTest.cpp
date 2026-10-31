#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/XmlResource.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskManager.h>

namespace BECore::Tests {

    // Compile-time checks: a regression breaks the build.
    static_assert(EnumUtils<ResourceState>::Count() == 4);
    static_assert(EnumUtils<ResourceState>::ToString(ResourceState::Loaded) == "Loaded");
    static_assert(EnumUtils<ResourceState>::ToString(ResourceState::Loading) == "Loading");
    static_assert(EnumUtils<ResourceState>::ToString(ResourceState::Unloaded) == "Unloaded");
    static_assert(EnumUtils<ResourceState>::ToString(ResourceState::Failed) == "Failed");
    static_assert(std::derived_from<XmlResource, IResource>);
    static_assert(std::is_constructible_v<ResourceHandle<XmlResource>>);

    namespace {

        constexpr const char* kExistingConfig = "config/ApplicationConfig.xml";

        // Not a lambda: a coroutine lambda would have to outlive its captures.
        Task<ResourceState> LoadExistingConfigAsync() {
            auto& rm = CoreManager::GetResourceManager();

            auto handle = co_await rm.LoadAsync<XmlResource>(kExistingConfig);
            if (!handle) {
                co_return ResourceState::Failed;
            }
            co_return handle->Get()->GetState();
        }

    }  // namespace

    /// Needs the live engine services: FileSystem with the config mount, ConfigManager, TaskManager.
    class ResourceManagerTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            // The cache is global; start every test from an empty one so the tests do not depend on order
            CoreManager::GetResourceManager().ClearCache();
        }
    };

    TEST_F(ResourceManagerTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    TEST_F(ResourceManagerTest, SyncLoading) {
        auto& rm = CoreManager::GetResourceManager();

        auto handle = rm.Load<XmlResource>(kExistingConfig);
        ASSERT_TRUE(handle) << "Failed to load " << kExistingConfig;
        EXPECT_EQ(handle->GetState(), ResourceState::Loaded);
        EXPECT_FALSE(handle->GetRoot().IsEmpty()) << "Empty root node";
    }

    TEST_F(ResourceManagerTest, AsyncLoading) {
        // The loader parks the coroutine on the thread pool (SwitchToBackground). Task::GetResult() would keep
        // resume()-ing it from this thread meanwhile, so run it through the TaskManager and wait on the handle.
        auto taskHandle = CoreManager::GetTaskManager().Run(LoadExistingConfigAsync());
        auto result = taskHandle->GetResult();

        ASSERT_TRUE(result.has_value()) << "Async load task failed";
        EXPECT_EQ(*result, ResourceState::Loaded) << "Resource not in Loaded state after async load";
    }

    TEST_F(ResourceManagerTest, Caching) {
        auto& rm = CoreManager::GetResourceManager();

        auto handle1 = rm.Load<XmlResource>(kExistingConfig);
        auto handle2 = rm.Load<XmlResource>(kExistingConfig);
        ASSERT_TRUE(handle1);
        ASSERT_TRUE(handle2);

        EXPECT_EQ(handle1.Get(), handle2.Get()) << "Cache miss: the resources are different instances";
        EXPECT_NE(rm.GetCache().GetCount(), 0u) << "Cache is empty after loading";
    }

    TEST_F(ResourceManagerTest, InvalidPath) {
        auto& rm = CoreManager::GetResourceManager();

        auto handle = rm.Load<XmlResource>("config/NonExistentFile.xml");

        const bool loaded = handle && handle->GetState() == ResourceState::Loaded;
        EXPECT_FALSE(loaded) << "A non-existent file must not load";
    }

    TEST_F(ResourceManagerTest, ResourceMemoryUsage) {
        auto& rm = CoreManager::GetResourceManager();

        auto handle = rm.Load<XmlResource>(kExistingConfig);
        ASSERT_TRUE(handle);

        EXPECT_NE(handle->GetMemoryUsage(), 0u) << "Memory usage of a loaded resource should be non-zero";
        EXPECT_NE(rm.GetCache().GetTotalMemoryUsage(), 0u) << "Total cache memory should be non-zero";
    }

}  // namespace BECore::Tests
