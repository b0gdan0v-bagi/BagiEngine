#include <BECore/Config/StringXmlWriter.h>
#include <BECore/File/File.h>
#include <BECore/File/FilePath.h>
#include <BECore/Reflection/XmlSerializer.h>

namespace BECore {

    namespace {

        /// The shortest text that reads back to the same number. pugixml's own `set_value(float)` is `%.9g` (116.67f becomes
        /// 116.669998), which rewrites every number a person typed in a file the editors save.
        template <typename T>
        String ShortestText(T value) {
            return Format("{}", value);
        }

        // The lookups of pugixml's child(name) and attribute(name) over a view, so no name is copied to get a NUL
        pugi::xml_node FindChild(pugi::xml_node parent, StringView name) {
            if (name.empty()) {
                return pugi::xml_node();
            }
            for (pugi::xml_node child = parent.first_child(); child; child = child.next_sibling()) {
                if (StringView(child.name()) == name) {
                    return child;
                }
            }
            return pugi::xml_node();
        }

        pugi::xml_attribute FindAttribute(pugi::xml_node node, StringView name) {
            if (name.empty()) {
                return pugi::xml_attribute();
            }
            for (pugi::xml_attribute attribute = node.first_attribute(); attribute; attribute = attribute.next_attribute()) {
                if (StringView(attribute.name()) == name) {
                    return attribute;
                }
            }
            return pugi::xml_attribute();
        }

        pugi::xml_node AppendElement(pugi::xml_node parent, StringView name) {
            pugi::xml_node element = parent.append_child(pugi::node_element);
            element.set_name(name.data(), name.size());
            return element;
        }

    }  // namespace

    XmlSerializer::XmlSerializer() {
        _nodeStack.push_back(_document.append_child("root"));
    }

    bool XmlSerializer::SaveToFile(const std::filesystem::path& path) const {
        // In place like pugi::save_file did; the path goes in as UTF-8, which save_file(const char*) is not on Windows
        auto file = File::Open(PathToUtf8(path), FileMode::Write);
        if (!file || !file->Write(SaveToString())) {
            return false;
        }
        return file->Close().has_value();
    }

    String XmlSerializer::SaveToString() const {
        StringXmlWriter writer;
        _document.save(writer, "  ");
        return eastl::move(writer.text);
    }

    pugi::xml_node XmlSerializer::GetOrCreateChild(StringView name) {
        if (_nodeStack.empty()) {
            return pugi::xml_node();
        }

        pugi::xml_node parent = _nodeStack.back();

        pugi::xml_node child = FindChild(parent, name);
        if (!child) {
            child = AppendElement(parent, name);
        }
        return child;
    }

    pugi::xml_attribute XmlSerializer::GetOrCreateAttribute(StringView name) {
        if (_nodeStack.empty()) {
            return pugi::xml_attribute();
        }

        pugi::xml_node node = _nodeStack.back();

        pugi::xml_attribute attr = FindAttribute(node, name);
        if (!attr) {
            attr = node.append_attribute("");
            attr.set_name(name.data(), name.size());
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
        node.text().set(ShortestText(value).c_str());
    }

    void XmlSerializer::Write(StringView name, double value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(ShortestText(value).c_str());
    }

    void XmlSerializer::Write(StringView name, StringView value) {
        pugi::xml_node node = GetOrCreateChild(name);
        node.text().set(value.data(), value.size());
    }

    void XmlSerializer::Write(StringView name, const PoolString& value) {
        pugi::xml_node node = GetOrCreateChild(name);
        const StringView text = value.ToStringView();
        node.text().set(text.data(), text.size());
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
        attr.set_value(ShortestText(value).c_str());
    }

    void XmlSerializer::WriteAttribute(StringView name, double value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(ShortestText(value).c_str());
    }

    void XmlSerializer::WriteAttribute(StringView name, StringView value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        attr.set_value(value.data(), value.size());
    }

    void XmlSerializer::WriteAttribute(StringView name, const PoolString& value) {
        pugi::xml_attribute attr = GetOrCreateAttribute(name);
        const StringView text = value.ToStringView();
        attr.set_value(text.data(), text.size());
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
        pugi::xml_node elem = AppendElement(_nodeStack.back(), ctx.elementName);
        _nodeStack.push_back(elem);
        return true;
    }

    void XmlSerializer::EndArrayElement() {
        if (_nodeStack.size() > 1) {
            _nodeStack.pop_back();
        }
    }

}  // namespace BECore
