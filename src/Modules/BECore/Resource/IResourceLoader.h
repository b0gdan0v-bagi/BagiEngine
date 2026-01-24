#pragma once

#include <BECore/Resource/IResource.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <TaskSystem/Task.h>
#include <EASTL/string_view.h>

namespace BECore {

    /**
     * @brief Base interface for resource loaders
     * 
     * Each loader handles specific file types and creates
     * corresponding resource objects. Loaders are registered
     * with ResourceManager and selected based on file extension.
     * 
     * @example
     * class XmlResourceLoader : public IResourceLoader {
     *     BE_CLASS(XmlResourceLoader)
     * public:
     *     bool CanLoad(eastl::string_view extension) const override {
     *         return extension == ".xml";
     *     }
     *     Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override {
     *         // Load XML asynchronously...
     *     }
     * };
     */
    class IResourceLoader : public RefCounted {
        BE_CLASS(IResourceLoader, FACTORY_BASE)
    public:
        virtual ~IResourceLoader() = default;
        
        /**
         * @brief Check if this loader can handle given extension
         * 
         * @param extension File extension (e.g., ".xml", ".png", ".bin")
         * @return true if this loader supports the extension
         */
        virtual bool CanLoad(eastl::string_view extension) const = 0;
        
        /**
         * @brief Load resource asynchronously
         * 
         * Runs on TaskSystem thread pool. Should not block the calling thread.
         * 
         * @param path Virtual path to the resource (resolved via FileSystem)
         * @return Task that resolves to loaded resource or nullptr on failure
         */
        virtual Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) = 0;
        
        /**
         * @brief Load resource synchronously (blocking)
         * 
         * Blocking operation. Use LoadAsync for better performance.
         * 
         * @param path Virtual path to the resource (resolved via FileSystem)
         * @return Loaded resource or nullptr on failure
         */
        virtual IntrusivePtr<IResource> LoadSync(PoolString path) = 0;
    };

}  // namespace BECore
