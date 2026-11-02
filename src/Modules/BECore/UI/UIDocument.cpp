#include "UIDocument.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <Generated/UIDocument.gen.hpp>

namespace BECore {

    IntrusivePtr<UIDocument> UIDocument::Create() {
        return IntrusivePtr<UIDocument>(new UIDocument());
    }

    ResourceState UIDocument::GetState() const {
        return _state;
    }

    PoolString UIDocument::GetPath() const {
        return _path;
    }

    uint64_t UIDocument::GetMemoryUsage() const {
        return sizeof(*this) + _path.ToStringView().size();
    }

    PoolString UIDocument::GetTypeName() const {
        return PoolString::Intern("UIDocument");
    }

    bool UIDocument::Instantiate(UIScreenContent& content) const {
        content = UIScreenContent{};
        if (_state != ResourceState::Loaded) {
            return false;
        }

        XmlDeserializer deserializer;
        if (!deserializer.LoadFromXmlNode(_config.GetRoot())) {
            LOG_ERROR("UIDocument: '{}' has no root element"_format(_path.ToStringView()));
            return false;
        }
        content.Deserialize(deserializer);

        for (const auto& error : deserializer.GetErrors()) {
            LOG_ERROR("UIDocument: '{}' line {}: {} ({})"_format(_path.ToStringView(), error.line, error.errorMessage.ToStringView(), error.path.ToStringView()));
        }
        return !deserializer.HasErrors();
    }

    void UIDocument::SetLoaded(PoolString path, XmlConfig config) {
        _path = path;
        _config = std::move(config);
        _state = ResourceState::Loaded;
    }

    void UIDocument::SetFailed(PoolString path) {
        _path = path;
        _state = ResourceState::Failed;
    }

}  // namespace BECore
