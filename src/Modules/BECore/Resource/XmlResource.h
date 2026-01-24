#pragma once

#include <BECore/Resource/IResource.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Config/XmlConfig.h>
#include <BECore/Config/XmlNode.h>

namespace BECore {

    /**
     * @brief XML configuration file as a managed resource
     * 
     * Wraps XmlConfig for caching through ResourceManager.
     * Provides access to root XmlNode for reading configuration.
     * Immutable after loading, safe for concurrent access.
     * 
     * @example
     * auto handle = resourceManager.Load<XmlResource>("config/game.xml");
     * if (handle && handle->GetState() == ResourceState::Loaded) {
     *     auto root = handle->GetRoot();
     *     auto title = root.ParseAttribute<eastl::string_view>("title");
     * }
     */
    class XmlResource : public IResource {
        BE_CLASS(XmlResource)
    public:
        ~XmlResource() override = default;
        
        // IResource interface
        ResourceState GetState() const override;
        PoolString GetPath() const override;
        uint64_t GetMemoryUsage() const override;
        PoolString GetTypeName() const override;
        
        /**
         * @brief Get root XML node for reading
         * 
         * @return Root XmlNode of the document
         * @note Only valid if GetState() == ResourceState::Loaded
         */
        XmlNode GetRoot() const;
        
        /**
         * @brief Get underlying XmlConfig
         * 
         * @return Reference to internal XmlConfig
         */
        const XmlConfig& GetConfig() const;

        /**
         * @brief Create a new XmlResource instance
         * 
         * Used by XmlResourceLoader for resource creation.
         * 
         * @return IntrusivePtr to new XmlResource
         */
        static IntrusivePtr<XmlResource> Create();

    private:
        friend class XmlResourceLoader;
        
        // Private constructor for XmlResourceLoader
        XmlResource() : _config(XmlConfig::Create()) {}
        
        /**
         * @brief Initialize resource with loaded XML
         * 
         * Called by XmlResourceLoader after successful load.
         * 
         * @param path Virtual path to the resource
         * @param config Loaded XmlConfig
         */
        void SetLoaded(PoolString path, XmlConfig config);
        
        /**
         * @brief Mark resource as failed
         * 
         * Called by XmlResourceLoader on load failure.
         * 
         * @param path Virtual path to the resource
         */
        void SetFailed(PoolString path);
        
        PoolString _path;
        XmlConfig _config;
        ResourceState _state = ResourceState::Unloaded;
    };

}  // namespace BECore
