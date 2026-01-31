#pragma once

#include <BECore/Config/XmlConfig.h>
#include <BECore/Config/XmlNode.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <TaskSystem/Task.h>
#include <mutex>

namespace BECore {

    /**
     * @brief Manager for loading and caching all XML configurations
     * 
     * Scans config/ directory recursively and loads all XML files in parallel
     * using TaskManager. Provides centralized access to all configs by name.
     * 
     * @note Access via CoreManager::GetConfigManager()
     * 
     * @example
     * // Get config by name (without extension)
     * auto rootNode = CoreManager::GetConfigManager().GetConfig("LoggerConfig"_ps);
     * if (rootNode) {
     *     // Use config...
     * }
     */
    class ConfigManager {
    public:
        ConfigManager() = default;
        ~ConfigManager() = default;

        // Non-copyable, non-movable
        ConfigManager(const ConfigManager&) = delete;
        ConfigManager& operator=(const ConfigManager&) = delete;
        ConfigManager(ConfigManager&&) = delete;
        ConfigManager& operator=(ConfigManager&&) = delete;

        /**
         * @brief Initialize - scan and load all XML files from config/
         * 
         * Uses TaskManager for parallel loading.
         * Blocks until all configs are loaded.
         * Safe to call multiple times - subsequent calls are no-ops.
         */
        void Initialize();

        /**
         * @brief Get loaded config by name (without extension)
         * @param name Config name (e.g., "LoggerConfig")
         * @return Root XmlNode or invalid node if not found
         */
        XmlNode GetConfig(PoolString name) const;

        /**
         * @brief Check if config exists
         * @param name Config name (without extension)
         * @return True if config is loaded
         */
        bool HasConfig(PoolString name) const;

        /**
         * @brief Get number of loaded configs
         */
        size_t GetConfigCount() const;

    private:
        /**
         * @brief Scan directory and collect XML files to load
         * @param dir Directory to scan
         * @param filesToLoad Output vector of (path, name) pairs
         */
        void ScanDirectory(const std::filesystem::path& dir,
                          eastl::vector<eastl::pair<std::filesystem::path, PoolString>>& filesToLoad) const;

        /**
         * @brief Load single config file asynchronously
         * @param path Physical path to XML file
         * @param name Config name (stem without extension)
         */
        Task<void> LoadConfigAsync(std::filesystem::path path, PoolString name);

        mutable std::mutex _mutex;
        UnorderedPoolMap<IntrusivePtrAtomic<XmlDocument>> _configs;
        bool _initialized = false;
    };

}  // namespace BECore
