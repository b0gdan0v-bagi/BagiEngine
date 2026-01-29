#include <BECore/Reflection/XmlSerializer.h>
#include <sstream>

namespace BECore {

    XmlSerializer::XmlSerializer() {
        // Initialize with root node
        _nodeStack.push_back(_document.append_child("root"));
    }

    bool XmlSerializer::SaveToFile(const std::filesystem::path& path) const {
        return _document.save_file(path.c_str(), "  ");
    }

    eastl::string XmlSerializer::SaveToString() const {
        std::ostringstream oss;
        _document.save(oss, "  ");
        std::string stdStr = oss.str();
        return eastl::string(stdStr.c_str(), stdStr.size());
    }

    pugi::xml_node XmlSerializer::GetOrCreateChild(eastl::string_view name) {
        if (_nodeStack.empty()) {
            return pugi::xml_node();
        }
        
        pugi::xml_node parent = _nodeStack.back();
        std::string nameStr(name.data(), name.size());
        
        pugi::xml_node child = parent.child(nameStr.c_str());
        if (!child) {
            child = parent.append_child(nameStr.c_str());
        }
        return child;
    }

    pugi::xml_attribute XmlSerializer::GetOrCreateAttribute(eastl::string_view name) {
        if (_nodeStack.empty()) {
            return pugi::xml_attribute();
        }
        
        pugi::xml_node node = _nodeStack.back();
        std::string nameStr(name.data(), name.size());
        
        pugi::xml_attribute attr = node.attribute(nameStr.c_str());
        if (!attr) {
            attr = node.append_attribute(nameStr.c_str());
        }
        return attr;
    }

    // =============================================================================
    // Write methods (child elements)
    // =============================================================================

    void XmlSerializer::Write(eastl::string_view name, bool value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value ? "true" : "false");
    }

    void XmlSerializer::Write(eastl::string_view name, int8_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<int32_t>(value));
    }

    void XmlSerializer::Write(eastl::string_view name, uint8_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<unsigned int>(value));
    }

    void XmlSerializer::Write(eastl::string_view name, int16_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<int>(value));
    }

    void XmlSerializer::Write(eastl::string_view name, uint16_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<unsigned int>(value));
    }

    void XmlSerializer::Write(eastl::string_view name, int32_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value);
    }

    void XmlSerializer::Write(eastl::string_view name, uint32_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value);
    }

    void XmlSerializer::Write(eastl::string_view name, int64_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<long long>(value));
    }

    void XmlSerializer::Write(eastl::string_view name, uint64_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<unsigned long long>(value));
    }

    void XmlSerializer::Write(eastl::string_view name, float value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value);
    }

    void XmlSerializer::Write(eastl::string_view name, double value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value);
    }

    void XmlSerializer::Write(eastl::string_view name, eastl::string_view value) {
        pugi::xml_node node = GetOrCreateChild(name);
        std::string valueStr(value.data(), value.size());
        node.text().set(valueStr.c_str());
    }

    void XmlSerializer::Write(eastl::string_view name, const PoolString& value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value.ToStringView().data());
    }

    // =============================================================================
    // WriteAttribute methods (XML attributes)
    // =============================================================================

    void XmlSerializer::WriteAttribute(eastl::string_view name, bool value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value ? "true" : "false");
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, int8_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<int>(value));
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, uint8_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<unsigned int>(value));
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, int16_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<int>(value));
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, uint16_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<unsigned int>(value));
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, int32_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value);
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, uint32_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value);
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, int64_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<long long>(value));
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, uint64_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<unsigned long long>(value));
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, float value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value);
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, double value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value);
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, eastl::string_view value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        std::string valueStr(value.data(), value.size());
        attr.set_value(valueStr.c_str());
    }

    void XmlSerializer::WriteAttribute(eastl::string_view name, const PoolString& value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value.ToStringView().data());
    }

    // =============================================================================
    // Object nesting
    // =============================================================================

    bool XmlSerializer::BeginObject(eastl::string_view name) {
        pugi::xml_node node = GetOrCreateChild(name);
        _nodeStack.push_back(node);
        return true;
    }

    void XmlSerializer::EndObject() {
        if (_nodeStack.size() > 1) {
            _nodeStack.pop_back();
        }
    }

    // =============================================================================
    // Array support
    // =============================================================================

    bool XmlSerializer::BeginArray(eastl::string_view name, eastl::string_view elementName, size_t& count) {
        std::string nameStr(name.data(), name.size());
        
        pugi::xml_node node = GetOrCreateChild(name);
        node.append_attribute("count").set_value(static_cast<unsigned int>(count));
        _nodeStack.push_back(node);
        
        // Store array context for element creation
        ArrayContext ctx;
        ctx.parentNode = node;
        ctx.elementName = eastl::string(elementName.data(), elementName.size());
        ctx.currentIndex = 0;
        _arrayStack.push_back(ctx);
        
        return true;
    }

    void XmlSerializer::EndArray() {
        if (!_arrayStack.empty()) {
            _arrayStack.pop_back();
        }
        if (_nodeStack.size() > 1) {
            _nodeStack.pop_back();
        }
    }

}  // namespace BECore
