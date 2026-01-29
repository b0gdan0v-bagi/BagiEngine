#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Config/XmlNode.h>
#include <BECore/GameManager/CoreManager.h>

namespace BECore {

    XmlDeserializer::XmlDeserializer() {
    }

    bool XmlDeserializer::LoadFromFile(const std::filesystem::path& path) {
        pugi::xml_parse_result result = _document.load_file(path.c_str());
        if (!result) {
            AddError("", "Failed to parse XML file");
            return false;
        }
        
        _nodeStack.clear();
        _nodeStack.push_back(_document.document_element());
        return true;
    }

    bool XmlDeserializer::LoadFromVirtualPath(eastl::string_view virtualPath) {
        auto realPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        if (realPath.empty()) {
            AddError("", "Failed to resolve virtual path");
            return false;
        }
        return LoadFromFile(realPath);
    }

    bool XmlDeserializer::LoadFromString(eastl::string_view xmlContent) {
        pugi::xml_parse_result result = _document.load_buffer(
            xmlContent.data(), 
            xmlContent.size()
        );
        if (!result) {
            AddError("", "Failed to parse XML string");
            return false;
        }
        
        _nodeStack.clear();
        _nodeStack.push_back(_document.document_element());
        return true;
    }

    bool XmlDeserializer::LoadFromXmlNode(const XmlNode& node) {
        pugi::xml_node pugiNode = node.GetPugiNode();
        if (!pugiNode) {
            AddError("", "Invalid XmlNode");
            return false;
        }
        
        _nodeStack.clear();
        _nodeStack.push_back(pugiNode);
        return true;
    }

    pugi::xml_node XmlDeserializer::GetChild(eastl::string_view name) const {
        if (_nodeStack.empty()) {
            return pugi::xml_node();
        }
        
        std::string nameStr(name.data(), name.size());
        return _nodeStack.back().child(nameStr.c_str());
    }

    pugi::xml_attribute XmlDeserializer::GetAttribute(eastl::string_view name) const {
        if (_nodeStack.empty()) {
            return pugi::xml_attribute();
        }
        
        std::string nameStr(name.data(), name.size());
        return _nodeStack.back().attribute(nameStr.c_str());
    }

    void XmlDeserializer::AddError(eastl::string_view fieldName, eastl::string_view errorMessage) {
        DeserializeError error;
        error.fieldName = PoolString::Intern(fieldName);
        error.errorMessage = PoolString::Intern(errorMessage);
        error.line = 0;  // TODO: Get line number from pugixml if available
        _errors.push_back(error);
    }

    // =============================================================================
    // Read methods (child elements)
    // =============================================================================

    bool XmlDeserializer::Read(eastl::string_view name, bool& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        eastl::string_view text = node.text().as_string();
        outValue = (text == "true" || text == "1");
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, int8_t& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = static_cast<int8_t>(node.text().as_int());
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, uint8_t& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = static_cast<uint8_t>(node.text().as_uint());
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, int16_t& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = static_cast<int16_t>(node.text().as_int());
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, uint16_t& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = static_cast<uint16_t>(node.text().as_uint());
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, int32_t& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = node.text().as_int();
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, uint32_t& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = node.text().as_uint();
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, int64_t& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = static_cast<int64_t>(node.text().as_llong());
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, uint64_t& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = static_cast<uint64_t>(node.text().as_ullong());
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, float& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = node.text().as_float();
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, double& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        outValue = node.text().as_double();
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, eastl::string& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        const char* text = node.text().as_string();
        outValue = eastl::string(text);
        return true;
    }

    bool XmlDeserializer::Read(eastl::string_view name, PoolString& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            AddError(name, "Element not found");
            return false;
        }
        
        const char* text = node.text().as_string();
        outValue = PoolString::Intern(eastl::string_view{text});
        return true;
    }

    // =============================================================================
    // ReadAttribute methods (XML attributes)
    // =============================================================================

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, bool& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            // Don't add error for missing attributes (they might be optional)
            return false;
        }
        
        eastl::string_view text = attr.as_string();
        outValue = (text == "true" || text == "1");
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, int8_t& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = static_cast<int8_t>(attr.as_int());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, uint8_t& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = static_cast<uint8_t>(attr.as_uint());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, int16_t& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = static_cast<int16_t>(attr.as_int());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, uint16_t& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = static_cast<uint16_t>(attr.as_uint());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, int32_t& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = attr.as_int();
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, uint32_t& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = attr.as_uint();
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, int64_t& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = static_cast<int64_t>(attr.as_llong());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, uint64_t& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = static_cast<uint64_t>(attr.as_ullong());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, float& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = attr.as_float();
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, double& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        outValue = attr.as_double();
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, eastl::string& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        const char* text = attr.as_string();
        outValue = eastl::string(text);
        return true;
    }

    bool XmlDeserializer::ReadAttribute(eastl::string_view name, PoolString& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        
        const char* text = attr.as_string();
        outValue = PoolString::Intern(eastl::string_view{text});
        return true;
    }

    // =============================================================================
    // Object nesting
    // =============================================================================

    bool XmlDeserializer::BeginObject(eastl::string_view name) {
        pugi::xml_node node = GetChild(name);
        if (node) {
            _nodeStack.push_back(node);
            return true;
        }
        return false;
    }

    void XmlDeserializer::EndObject() {
        if (_nodeStack.size() > 1) {
            _nodeStack.pop_back();
        }
    }

    // =============================================================================
    // Array support
    // =============================================================================

    bool XmlDeserializer::BeginArray(eastl::string_view name, eastl::string_view elementName, size_t& count) {
        std::string nameStr(name.data(), name.size());
        
        pugi::xml_node node = GetChild(name);
        if (node) {
            count = node.attribute("count").as_uint();
            
            // Collect child elements for iteration
            ArrayContext ctx;
            ctx.parentNode = node;
            ctx.currentIndex = 0;
            for (pugi::xml_node child : node.children()) {
                ctx.elements.push_back(child);
            }
            _arrayStack.push_back(eastl::move(ctx));
            
            _nodeStack.push_back(node);
            return true;
        }
        return false;
    }

    void XmlDeserializer::EndArray() {
        if (!_arrayStack.empty()) {
            _arrayStack.pop_back();
        }
        if (_nodeStack.size() > 1) {
            _nodeStack.pop_back();
        }
    }

    // =============================================================================
    // Error tracking
    // =============================================================================

    bool XmlDeserializer::HasErrors() const {
        return !_errors.empty();
    }

    const eastl::vector<DeserializeError>& XmlDeserializer::GetErrors() const {
        return _errors;
    }

    void XmlDeserializer::ClearErrors() {
        _errors.clear();
    }

}  // namespace BECore
