#include "ResourceManagerTest.h"
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/Logger.h>
#include <BECore/Config/XmlConfig.h>

#include <Generated/ResourceManagerTest.gen.hpp>

namespace BECore::Tests {


    bool ResourceManagerTest::Run() {
        LOG_INFO("Running ResourceManagerTest...");
        
        // Run compile-time tests
        TestCompileTime();
        
        // Run runtime tests
        bool success = true;
        
        success &= TestSyncLoading();
        success &= TestAsyncLoading();
        success &= TestCaching();
        success &= TestInvalidPath();
        success &= TestResourceMemoryUsage();
        
        if (success) {
            LOG_INFO("✓ All ResourceManager tests passed");
        } else {
            LOG_ERROR("✗ Some ResourceManager tests failed");
        }
        
        return success;
    }

    bool ResourceManagerTest::TestSyncLoading() {
        LOG_INFO("Testing sync loading...");
        
        auto& rm = CoreManager::GetResourceManager();
        
        // Load a test XML file as XmlResource (ApplicationConfig should exist)
        auto handle = rm.Load<XmlResource>("config/ApplicationConfig.xml");
        
        if (!handle) {  
            LOG_ERROR("Failed to load ApplicationConfig.xml");
            return false;
        }
        
        if (handle->GetState() != ResourceState::Loaded) {
            LOG_ERROR("Resource not in Loaded state");
            return false;
        }
        
        auto root = handle->GetRoot();
        if (root.IsEmpty()) {
            LOG_ERROR("Empty root node");
            return false;
        }
        
        LOG_INFO("✓ Sync loading test passed");
        return true;
    }

    bool ResourceManagerTest::TestAsyncLoading() {
        LOG_INFO("Testing async loading...");
        
        auto TestAsync = []() -> Task<bool> {
            auto& rm = CoreManager::GetResourceManager();
            
            // Load a test XML file asynchronously
            auto handle = co_await rm.LoadAsync<XmlResource>("config/ApplicationConfig.xml");
            
            if (!handle) {
                LOG_ERROR("Async load failed");
                co_return false;
            }
            
            if (handle->Get()->GetState() != ResourceState::Loaded) {
                LOG_ERROR("Resource not in Loaded state after async load");
                co_return false;
            }
            
            LOG_INFO("✓ Async loading test passed");
            co_return true;
        };
        
        // Run the async test
        auto task = TestAsync();
        auto result = task.GetResult();
        
        if (!result.has_value()) {
            LOG_ERROR("Async test task failed");
            return false;
        }
        
        return result.value();
    }

    bool ResourceManagerTest::TestCaching() {
        LOG_INFO("Testing resource caching...");
        
        auto& rm = CoreManager::GetResourceManager();
        
        // Load the same resource twice
        auto handle1 = rm.Load<XmlResource>("config/ApplicationConfig.xml");
        auto handle2 = rm.Load<XmlResource>("config/ApplicationConfig.xml");
        
        if (!handle1 || !handle2) {
            LOG_ERROR("Failed to load resources for cache test");
            return false;
        }
        
        // Both handles should point to the same resource instance
        if (handle1.Get() != handle2.Get()) {
            LOG_ERROR("Cache miss - resources are different instances");
            return false;
        }
        
        // Check cache statistics
        const auto& cache = rm.GetCache();
        if (cache.GetCount() == 0) {
            LOG_ERROR("Cache is empty after loading");
            return false;
        }
        
        LOG_INFO(Format("✓ Caching test passed (cache size: {})", cache.GetCount()).c_str());
        return true;
    }

    bool ResourceManagerTest::TestInvalidPath() {
        LOG_INFO("Testing invalid path handling...");
        
        auto& rm = CoreManager::GetResourceManager();
        
        // Try to load non-existent file
        auto handle = rm.Load<XmlResource>("config/NonExistentFile.xml");
        
        if (handle && handle->GetState() == ResourceState::Loaded) {
            LOG_ERROR("Invalid file loaded successfully - should have failed");
            return false;
        }
        
        LOG_INFO("✓ Invalid path test passed");
        return true;
    }

    bool ResourceManagerTest::TestResourceMemoryUsage() {
        LOG_INFO("Testing resource memory usage tracking...");
        
        auto& rm = CoreManager::GetResourceManager();
        
        // Clear cache first
        rm.ClearCache();
        
        // Load a resource
        auto handle = rm.Load<XmlResource>("config/ApplicationConfig.xml");
        
        if (!handle) {
            LOG_ERROR("Failed to load resource for memory test");
            return false;
        }
        
        // Check memory usage
        uint64_t memUsage = handle->GetMemoryUsage();
        if (memUsage == 0) {
            LOG_ERROR("Memory usage is zero - should be non-zero");
            return false;
        }
        
        // Check cache total memory
        const auto& cache = rm.GetCache();
        uint64_t totalMem = cache.GetTotalMemoryUsage();
        if (totalMem == 0) {
            LOG_ERROR("Total cache memory is zero");
            return false;
        }
        
        LOG_INFO(Format("✓ Memory usage test passed (resource: {} bytes, cache total: {} bytes)", 
                 memUsage, totalMem).c_str());
        return true;
    }

}  // namespace BECore::Tests
