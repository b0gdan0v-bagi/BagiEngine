#include "XmlConfig.h"

#include <Core/Config/XmlConfigImpl.h>
#include <Core/GameManager/CoreManager.h>

namespace Core {

    XmlConfig XmlConfig::Create() {
        auto impl = Core::New<XmlConfigImpl>(pugi::xml_document{});
        return XmlConfig(std::move(impl));
    }

    bool XmlConfig::LoadFromVirtualPath(std::string_view virtualPath) const {
        if (!_impl) {
            return false;
        }
        std::filesystem::path resolvedPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        if (resolvedPath.empty()) {
            return {};
        }
        return _impl->LoadFromFile(resolvedPath);
    }

    bool XmlConfig::LoadFromString(std::string_view xmlContent) const {
        if (!_impl) {
            return false;
        }
        return _impl->LoadFromString(xmlContent);
    }

    bool XmlConfig::SaveToFile(const std::filesystem::path& filepath) const {
        if (!_impl) {
            return false;
        }
        return _impl->SaveToFile(filepath);
    }

    XmlNode XmlConfig::GetRoot() const {
        if (!_impl) {
            return {};
        }
        return _impl->GetRoot();
    }

    void XmlConfig::Clear() const {
        if (_impl) {
            _impl->Clear();
        }
    }

    bool XmlConfig::IsLoaded() const {
        if (!_impl) {
            return false;
        }
        return _impl->IsLoaded();
    }

}  // namespace Core
