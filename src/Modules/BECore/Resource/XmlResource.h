#pragma once

#include <BECore/Config/XmlConfig.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Resource/IResource.h>
#include <cstdint>

namespace BECore {

    /** @brief An XML file held as an XmlConfig, cached by ResourceManager; read-only after loading. */
    class XmlResource : public IResource {
        BE_CLASS(XmlResource)
    public:
        XmlResource() : _config(XmlConfig::Create()) {}
        ~XmlResource() override = default;

        ResourceState GetState() const override;
        PoolString GetPath() const override;
        uint64_t GetMemoryUsage() const override;
        PoolString GetTypeName() const override;

        /** @note Only valid when GetState() == ResourceState::Loaded. */
        XmlNode GetRoot() const;

        const XmlConfig& GetConfig() const;

        static IntrusivePtr<XmlResource> Create();

    private:
        friend class XmlResourceLoader;

        void SetLoaded(PoolString path, XmlConfig config);

        void SetFailed(PoolString path);

        PoolString _path;
        XmlConfig _config;
        ResourceState _state = ResourceState::Unloaded;
    };

}  // namespace BECore
