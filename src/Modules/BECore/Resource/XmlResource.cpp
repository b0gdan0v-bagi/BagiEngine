#include <BECore/Resource/XmlResource.h>
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
        // The size of the XML document itself is not counted.
        return sizeof(*this) + _path.ToStringView().size();
    }

    PoolString XmlResource::GetTypeName() const {
        return "XmlResource"_intern;
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
