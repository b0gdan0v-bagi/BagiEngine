#include <BECore/Resource/XmlResource.h>
#include <BECore/RefCounted/IntrusivePtr.h>

#include <Generated/IResource.gen.hpp>
#include <Generated/XmlResource.gen.hpp>

namespace BECore {

    IntrusivePtr<XmlResource> XmlResource::Create() {
        return IntrusivePtr<XmlResource>(new XmlResource());
    }

    ResourceState XmlResource::GetState() const {
        return _state;
    }

    PoolString XmlResource::GetPath() const {
        return _path;
    }

    uint64_t XmlResource::GetMemoryUsage() const {
        // Base size + path + approximate XML document size
        // For more precise measurement, would need to track XmlConfig memory usage
        return sizeof(*this) + _path.ToStringView().size();
    }

    PoolString XmlResource::GetTypeName() const {
        return PoolString::Intern("XmlResource");
    }

    XmlNode XmlResource::GetRoot() const {
        return _config.GetRoot();
    }

    const XmlConfig& XmlResource::GetConfig() const {
        return _config;
    }

    void XmlResource::SetLoaded(PoolString path, XmlConfig config) {
        _path = path;
        _config = std::move(config);
        _state = ResourceState::Loaded;
    }

    void XmlResource::SetFailed(PoolString path) {
        _path = path;
        _state = ResourceState::Failed;
    }

}  // namespace BECore
