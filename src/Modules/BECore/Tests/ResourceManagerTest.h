#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/XmlResource.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Utils/EnumUtils.h>
#include <TaskSystem/Task.h>

namespace BECore::Tests {

    /**
     * @brief Tests for ResourceManager functionality
     * 
     * Tests async/sync loading, caching, and serialization integration.
     */
    class ResourceManagerTest : public ITest {
        BE_CLASS(ResourceManagerTest)
    public:
        ResourceManagerTest() = default;
        ~ResourceManagerTest() override = default;

        bool Run() override;
        
        eastl::string_view GetName() override {
            return GetStaticTypeName();
        }

        /**
         * @brief Compile-time validation tests
         */
        static constexpr void TestCompileTime() {
            // Validate ResourceState enum
            static_assert(EnumUtils<ResourceState>::Count() == 4);
            static_assert(EnumUtils<ResourceState>::ToString(ResourceState::Loaded) == "Loaded");
            static_assert(EnumUtils<ResourceState>::ToString(ResourceState::Loading) == "Loading");
            static_assert(EnumUtils<ResourceState>::ToString(ResourceState::Unloaded) == "Unloaded");
            static_assert(EnumUtils<ResourceState>::ToString(ResourceState::Failed) == "Failed");
            
            // Validate ResourceHandle constraints
            static_assert(std::derived_from<XmlResource, IResource>);
            
            // Validate resource handle can be constructed
            static_assert(std::is_constructible_v<ResourceHandle<XmlResource>>);
        }

    private:
        bool TestSyncLoading();
        bool TestAsyncLoading();
        bool TestCaching();
        bool TestInvalidPath();
        bool TestResourceMemoryUsage();
    };

    // Validate test interface
    static_assert(ValidTest<ResourceManagerTest>);

}  // namespace BECore::Tests
