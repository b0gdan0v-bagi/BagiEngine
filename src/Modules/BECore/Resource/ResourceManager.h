#pragma once

#include <BECore/Resource/IResource.h>
#include <BECore/Resource/IResourceLoader.h>
#include <BECore/Resource/ResourceCache.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Resource/SerializedResource.h>
#include <BECore/Resource/XmlResource.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Reflection/SaveSystem.h>
#include <BECore/Reflection/XmlArchive.h>
#include <BECore/Format/Format.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/Awaitables.h>
#include <EASTL/vector.h>

namespace BECore {

    /**
     * @brief Central manager for loading and caching resources
     * 
     * Provides async and sync resource loading with automatic caching.
     * Supports multiple resource types through registered loaders.
     * 
     * Thread-safe for concurrent loading operations.
     * Resources are cached and reused across multiple Load calls.
     * 
     * @note Access via CoreManager::GetResourceManager()
     * 
     * @example
     * // Async loading
     * Task<void> LoadAssets() {
     *     auto& rm = CoreManager::GetResourceManager();
     *     auto xml = co_await rm.LoadAsync<XmlResource>("config/game.xml");
     *     if (xml) {
     *         auto root = xml->GetRoot();
     *         // Use xml...
     *     }
     * }
     * 
     * // Sync loading
     * auto xml = CoreManager::GetResourceManager().Load<XmlResource>("config/game.xml");
     * 
     * // Serialized resource loading
     * auto player = co_await CoreManager::GetResourceManager()
     *     .LoadSerializedAsync<Player>("saves/player.xml");
     */
    class ResourceManager {
    public:
        ResourceManager() = default;
        ~ResourceManager() = default;
        
        // Non-copyable, non-movable
        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;
        ResourceManager(ResourceManager&&) = delete;
        ResourceManager& operator=(ResourceManager&&) = delete;
        
        /**
         * @brief Initialize resource manager and register default loaders
         * 
         * Should be called once during application startup.
         * Registers all loaders via generated ResourceLoaderFactory.
         */
        void Initialize();
        
        /**
         * @brief Load resource asynchronously with Task<T>
         * 
         * Checks cache first. If not cached, loads via appropriate loader.
         * 
         * @tparam T Resource type (must inherit from IResource)
         * @param path Virtual path to the resource
         * @return Task that resolves to ResourceHandle<T>
         */
        template<typename T>
            requires std::derived_from<T, IResource>
        Task<ResourceHandle<T>> LoadAsync(eastl::string_view path);
        
        /**
         * @brief Load resource synchronously (blocking)
         * 
         * Checks cache first. If not cached, loads via appropriate loader.
         * 
         * @tparam T Resource type (must inherit from IResource)
         * @param path Virtual path to the resource
         * @return ResourceHandle<T> (may be empty on failure)
         */
        template<typename T>
            requires std::derived_from<T, IResource>
        ResourceHandle<T> Load(eastl::string_view path);
        
        /**
         * @brief Load and deserialize object via Reflection system
         * 
         * Loads XML and deserializes into type T using reflection.
         * Convenient wrapper for loading configuration objects.
         * 
         * @tparam T Type with BE_CLASS and BE_REFLECT_FIELD
         * @param path Virtual path to XML/binary file
         * @return Task that resolves to deserialized object
         */
        template<HasReflection T>
        Task<T> LoadSerializedAsync(eastl::string_view path);
        
        /**
         * @brief Load and deserialize object synchronously
         * 
         * @tparam T Type with BE_CLASS and BE_REFLECT_FIELD
         * @param path Virtual path to XML/binary file
         * @return Deserialized object (default-constructed on failure)
         */
        template<HasReflection T>
        T LoadSerialized(eastl::string_view path);
        
        /**
         * @brief Register a resource loader
         * 
         * Loaders are checked in registration order.
         * 
         * @param loader Loader instance to register
         */
        void RegisterLoader(IntrusivePtr<IResourceLoader> loader);
        
        /**
         * @brief Clear all cached resources
         * 
         * Forces reload on next Load call.
         */
        void ClearCache();
        
        /**
         * @brief Get cache statistics
         * @return Reference to internal cache for inspection
         */
        const ResourceCache& GetCache() const;

    private:
        /**
         * @brief Find appropriate loader for file extension
         * @param extension File extension (e.g., ".xml")
         * @return Pointer to loader or nullptr if none found
         */
        IResourceLoader* FindLoader(eastl::string_view extension) const;
        
        /**
         * @brief Extract file extension from path
         * @param path File path
         * @return Extension including dot (e.g., ".xml")
         */
        eastl::string_view GetExtension(eastl::string_view path) const;
        
        ResourceCache _cache;
        eastl::vector<IntrusivePtr<IResourceLoader>> _loaders;
    };

    // Template implementations
    
    template<typename T>
        requires std::derived_from<T, IResource>
    Task<ResourceHandle<T>> ResourceManager::LoadAsync(eastl::string_view path) {
        // Convert string_view to PoolString for cache key
        PoolString pathKey = PoolString::Intern(path);
        
        // Check cache first
        if (auto cached = _cache.Get(pathKey)) {
            // Cast to requested type
            if (auto typed = dynamic_cast<T*>(cached.Get())) {
                co_return ResourceHandle<T>(IntrusivePtr<T>(typed));
            }
        }
        
        // Find appropriate loader
        auto extension = GetExtension(path);
        auto* loader = FindLoader(extension);
        if (!loader) {
            LOG_ERROR(Format("No loader found for extension: {}", extension).c_str());
            co_return ResourceHandle<T>();
        }
        
        // Load resource asynchronously
        auto resourceResult = co_await loader->LoadAsync(pathKey);
        if (!resourceResult.has_value() || !resourceResult.value()) {
            LOG_ERROR(Format("Failed to load resource: {}", path).c_str());
            co_return ResourceHandle<T>();
        }
        
        auto resource = resourceResult.value();
        
        // Cache the resource
        _cache.Put(pathKey, resource);
        
        // Cast to requested type
        if (auto typed = dynamic_cast<T*>(resource.Get())) {
            co_return ResourceHandle<T>(IntrusivePtr<T>(typed));
        }
        
        LOG_ERROR(Format("Resource type mismatch: {}", path).c_str());
        co_return ResourceHandle<T>();
    }
    
    template<typename T>
        requires std::derived_from<T, IResource>
    ResourceHandle<T> ResourceManager::Load(eastl::string_view path) {
        // Convert string_view to PoolString for cache key
        PoolString pathKey = PoolString::Intern(path);
        
        // Check cache first
        if (auto cached = _cache.Get(pathKey)) {
            // Cast to requested type
            if (auto typed = dynamic_cast<T*>(cached.Get())) {
                return ResourceHandle<T>(IntrusivePtr<T>(typed));
            }
        }
        
        // Find appropriate loader
        auto extension = GetExtension(path);
        auto* loader = FindLoader(extension);
        if (!loader) {
            LOG_ERROR(Format("No loader found for extension: {}", extension).c_str());
            return ResourceHandle<T>();
        }
        
        // Load resource synchronously
        auto resource = loader->LoadSync(pathKey);
        if (!resource) {
            LOG_ERROR(Format("Failed to load resource: {}", path).c_str());
            return ResourceHandle<T>();
        }
        
        // Cache the resource
        _cache.Put(pathKey, resource);
        
        // Cast to requested type
        if (auto typed = dynamic_cast<T*>(resource.Get())) {
            return ResourceHandle<T>(IntrusivePtr<T>(typed));
        }
        
        LOG_ERROR(Format("Resource type mismatch: {}", path).c_str());
        return ResourceHandle<T>();
    }
    
    template<HasReflection T>
    Task<T> ResourceManager::LoadSerializedAsync(eastl::string_view path) {
        // Switch to background thread for I/O
        co_await SwitchToBackground();
        
        // Create XmlArchive in Read mode and load from virtual path
        XmlArchive archive(XmlArchive::Mode::Read);
        if (!archive.LoadFromVirtualPath(path)) {
            LOG_ERROR(Format("Failed to load XML for serialization: {}", path).c_str());
            co_return T{};
        }
        
        // Deserialize using SerializeRoot
        T result{};
        SerializeRoot(archive, result);
        
        co_return result;
    }
    
    template<HasReflection T>
    T ResourceManager::LoadSerialized(eastl::string_view path) {
        // Create XmlArchive in Read mode and load from virtual path
        XmlArchive archive(XmlArchive::Mode::Read);
        if (!archive.LoadFromVirtualPath(path)) {
            LOG_ERROR(Format("Failed to load XML for serialization: {}", path).c_str());
            return T{};
        }
        
        // Deserialize using SerializeRoot
        T result{};
        SerializeRoot(archive, result);
        
        return result;
    }

}  // namespace BECore
