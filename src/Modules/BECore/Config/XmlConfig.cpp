#include "XmlConfig.h"

#include <BECore/Config/XmlDocument.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/RefCounted/New.h>

namespace BECore {

    XmlConfig XmlConfig::Create() {
        auto doc = BECore::New<XmlDocument>();
        return XmlConfig(std::move(doc));
    }

    bool XmlConfig::LoadFromVirtualPath(eastl::string_view virtualPath) const {
        if (!_doc) {
            return false;
        }
        std::filesystem::path resolvedPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        if (resolvedPath.empty()) {
            return false;
        }
        return _doc->LoadFromFile(resolvedPath);
    }

    bool XmlConfig::LoadFromFile(const std::filesystem::path& filepath) const {
        if (!_doc) {
            return false;
        }
        return _doc->LoadFromFile(filepath);
    }

    bool XmlConfig::LoadFromString(eastl::string_view xmlContent) const {
        if (!_doc) {
            return false;
        }
        return _doc->LoadFromString(xmlContent);
    }

    bool XmlConfig::SaveToFile(const std::filesystem::path& filepath) const {
        if (!_doc) {
            return false;
        }
        return _doc->SaveToFile(filepath);
    }

    XmlNode XmlConfig::GetRoot() const {
        if (!_doc) {
            return {};
        }
        return _doc->GetRoot();
    }

    void XmlConfig::Clear() const {
        if (_doc) {
            _doc->Clear();
        }
    }

    bool XmlConfig::IsLoaded() const {
        if (!_doc) {
            return false;
        }
        return _doc->IsLoaded();
    }

}  // namespace BECore
