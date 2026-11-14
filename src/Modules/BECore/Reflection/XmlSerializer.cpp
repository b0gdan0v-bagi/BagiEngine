#include <BECore/Reflection/XmlSerializer.h>

namespace BECore {

    XmlSerializer::XmlSerializer() {
        // Initialize with root node
        _nodeStack.push_back(_document.append_child("root"));
    }

    bool XmlSerializer::SaveToFile(const std::filesystem::path& path) const {
        return _document.save_file(path.c_str(), "  ");
    }

    String XmlSerializer::SaveToString() const {
        std::ostringstream oss;
        _document.save(oss, "  ");
        // std: the text of an std::ostringstream
        std::string stdStr = oss.str();
        return String(stdStr.c_str(), stdStr.size());
    }

    pugi::xml_node XmlSerializer::GetOrCreateChild(StringView name) {
        if (_nodeStack.empty()) {
            return pugi::xml_node();
        }

        pugi::xml_node parent = _nodeStack.back();
        String nameStr(name);

        pugi::xml_node child = parent.child(nameStr.c_str());
        if (!child) {
            child = parent.append_child(nameStr.c_str());
        }
        return child;
    }

    pugi::xml_attribute XmlSerializer::GetOrCreateAttribute(StringView name) {
        if (_nodeStack.empty()) {
            return pugi::xml_attribute();
        }

        pugi::xml_node node = _nodeStack.back();
        String nameStr(name);

        pugi::xml_attribute attr = node.attribute(nameStr.c_str());
        if (!attr) {
            attr = node.append_attribute(nameStr.c_str());
        }
        return attr;
    }

    // =============================================================================
    // Write methods (child elements)
    // =============================================================================

    void XmlSerializer::Write(StringView name, bool value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value ? "true" : "false");
    }

    void XmlSerializer::Write(StringView name, int8_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<int32_t>(value));
    }

    void XmlSerializer::Write(StringView name, uint8_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<unsigned int>(value));
    }

    void XmlSerializer::Write(StringView name, int16_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<int>(value));
    }

    void XmlSerializer::Write(StringView name, uint16_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<unsigned int>(value));
    }

    void XmlSerializer::Write(StringView name, int32_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value);
    }

    void XmlSerializer::Write(StringView name, uint32_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value);
    }

    void XmlSerializer::Write(StringView name, int64_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<long long>(value));
    }

    void XmlSerializer::Write(StringView name, uint64_t value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(static_cast<unsigned long long>(value));
    }

    void XmlSerializer::Write(StringView name, float value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value);
    }

    void XmlSerializer::Write(StringView name, double value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value);
    }

    void XmlSerializer::Write(StringView name, StringView value) {
        pugi::xml_node node = GetOrCreateChild(name);
        String valueStr(value);
        node.text().set(valueStr.c_str());
    }

    void XmlSerializer::Write(StringView name, const PoolString& value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value.ToStringView().data());
    }

    // =============================================================================
    // WriteAttribute methods (XML attributes)
    // =============================================================================

    void XmlSerializer::WriteAttribute(StringView name, bool value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value ? "true" : "false");
    }

    void XmlSerializer::WriteAttribute(StringView name, int8_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<int>(value));
    }

    void XmlSerializer::WriteAttribute(StringView name, uint8_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<unsigned int>(value));
    }

    void XmlSerializer::WriteAttribute(StringView name, int16_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<int>(value));
    }

    void XmlSerializer::WriteAttribute(StringView name, uint16_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<unsigned int>(value));
    }

    void XmlSerializer::WriteAttribute(StringView name, int32_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value);
    }

    void XmlSerializer::WriteAttribute(StringView name, uint32_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value);
    }

    void XmlSerializer::WriteAttribute(StringView name, int64_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<long long>(value));
    }

    void XmlSerializer::WriteAttribute(StringView name, uint64_t value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(static_cast<unsigned long long>(value));
    }

    void XmlSerializer::WriteAttribute(StringView name, float value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value);
    }

    void XmlSerializer::WriteAttribute(StringView name, double value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value);
    }

    void XmlSerializer::WriteAttribute(StringView name, StringView value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        String valueStr(value);
        attr.set_value(valueStr.c_str());
    }

    void XmlSerializer::WriteAttribute(StringView name, const PoolString& value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value.ToStringView().data());
    }

    // =============================================================================
    // Object nesting
    // =============================================================================

    bool XmlSerializer::BeginObject(StringView name) {
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

    bool XmlSerializer::BeginArray(StringView name, StringView elementName, size_t& count) {
        pugi::xml_node node = GetOrCreateChild(name);
        _nodeStack.push_back(node);

        ArrayContext ctx;
        ctx.parentNode = node;
        ctx.elementName = String(elementName.data(), elementName.size());
        ctx.currentIndex = 0;
        _arrayStack.push_back(eastl::move(ctx));

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

    bool XmlSerializer::BeginArrayElement() {
        if (_arrayStack.empty()) {
            return false;
        }
        const ArrayContext& ctx = _arrayStack.back();
        String elemName(ctx.elementName);
        pugi::xml_node elem = _nodeStack.back().append_child(elemName.c_str());
        _nodeStack.push_back(elem);
        return true;
    }

    void XmlSerializer::EndArrayElement() {
        if (_nodeStack.size() > 1) {
            _nodeStack.pop_back();
        }
    }

}  // namespace BECore
