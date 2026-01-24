#pragma once

#include <BECore/Resource/IResourceLoader.h>
#include <BECore/Resource/XmlResource.h>
#include <BECore/Logger/Logger.h>
#include <BECore/Format/Format.h>

namespace BECore {

    /**
     * @brief Resource loader for XML configuration files
     * 
     * Loads XML files via FileSystem and creates XmlResource instances.
     * Supports async and sync loading modes.
     * 
     * @example
     * auto loader = New<XmlResourceLoader>();
     * auto resource = co_await loader->LoadAsync("config/game.xml");
     */
    class XmlResourceLoader : public IResourceLoader {
        BE_CLASS(XmlResourceLoader)
    public:
        XmlResourceLoader() = default;
        ~XmlResourceLoader() override = default;
        
        /**
         * @brief Check if this loader can handle given extension
         * @param extension File extension
         * @return true if extension is ".xml"
         */
        bool CanLoad(eastl::string_view extension) const override;
        
        /**
         * @brief Load XML resource asynchronously
         * @param path Virtual path to XML file
         * @return Task that resolves to XmlResource
         */
        Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override;
        
        /**
         * @brief Load XML resource synchronously
         * @param path Virtual path to XML file
         * @return XmlResource or nullptr on failure
         */
        IntrusivePtr<IResource> LoadSync(PoolString path) override;

    private:
        /**
         * @brief Internal loading implementation
         * @param path Virtual path to XML file
         * @return Loaded XmlResource or nullptr on failure
         */
        IntrusivePtr<XmlResource> LoadInternal(PoolString path);
    };

}  // namespace BECore
