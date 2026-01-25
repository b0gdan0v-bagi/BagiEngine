#include <BECore/Reflection/XmlArchive.h>
#include <BECore/Config/XmlNode.h>
#include <BECore/GameManager/CoreManager.h>
#include <sstream>

namespace BECore {

    XmlArchive::XmlArchive(Mode mode)
        : _mode(mode)
    {
        // Initialize with root node
        if (_mode == Mode::Write) {
            _nodeStack.push_back(_document.append_child("root"));
        }
    }

    bool XmlArchive::LoadFromFile(const std::filesystem::path& path) {
        pugi::xml_parse_result result = _document.load_file(path.c_str());
        if (!result) {
            return false;
        }
        
        _nodeStack.clear();
        _nodeStack.push_back(_document.document_element());
        return true;
    }

    bool XmlArchive::LoadFromVirtualPath(eastl::string_view virtualPath) {
        auto realPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        if (realPath.empty()) {
            return false;
        }
        return LoadFromFile(realPath);
    }

    bool XmlArchive::LoadFromString(eastl::string_view xmlContent) {
        pugi::xml_parse_result result = _document.load_buffer(
            xmlContent.data(), 
            xmlContent.size()
        );
        if (!result) {
            return false;
        }
        
        _nodeStack.clear();
        _nodeStack.push_back(_document.document_element());
        return true;
    }

    bool XmlArchive::LoadFromXmlNode(const XmlNode& node) {
        pugi::xml_node pugiNode = node.GetPugiNode();
        if (!pugiNode) {
            return false;
        }
        
        _nodeStack.clear();
        _nodeStack.push_back(pugiNode);
        return true;
    }

    bool XmlArchive::SaveToFile(const std::filesystem::path& path) const {
        return _document.save_file(path.c_str(), "  ");
    }

    eastl::string XmlArchive::SaveToString() const {
        std::ostringstream oss;
        _document.save(oss, "  ");
        std::string stdStr = oss.str();
        return eastl::string(stdStr.c_str(), stdStr.size());
    }

    pugi::xml_node XmlArchive::GetOrCreateChild(eastl::string_view name) {
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

    pugi::xml_node XmlArchive::GetChild(eastl::string_view name) const {
        if (_nodeStack.empty()) {
            return pugi::xml_node();
        }
        
        std::string nameStr(name.data(), name.size());
        return _nodeStack.back().child(nameStr.c_str());
    }

    pugi::xml_attribute XmlArchive::GetOrCreateAttribute(eastl::string_view name) {
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

    pugi::xml_attribute XmlArchive::GetAttribute(eastl::string_view name) const {
        if (_nodeStack.empty()) {
            return pugi::xml_attribute();
        }
        
        std::string nameStr(name.data(), name.size());
        return _nodeStack.back().attribute(nameStr.c_str());
    }

    // =============================================================================
    // Primitive serialization implementations (child elements)
    // =============================================================================

    void XmlArchive::Serialize(eastl::string_view name, bool& value) {
        std::string nameStr(name.data(), name.size());
        
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(value ? "true" : "false");
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                eastl::string_view text = node.text().as_string();
                value = (text == "true" || text == "1");
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, int8_t& value) {
        int32_t temp = value;
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(temp);
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = static_cast<int8_t>(node.text().as_int());
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, uint8_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(static_cast<unsigned int>(value));
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = static_cast<uint8_t>(node.text().as_uint());
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, int16_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(static_cast<int>(value));
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = static_cast<int16_t>(node.text().as_int());
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, uint16_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(static_cast<unsigned int>(value));
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = static_cast<uint16_t>(node.text().as_uint());
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, int32_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(value);
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = node.text().as_int();
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, uint32_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(value);
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = node.text().as_uint();
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, int64_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(static_cast<long long>(value));
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = static_cast<int64_t>(node.text().as_llong());
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, uint64_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(static_cast<unsigned long long>(value));
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = static_cast<uint64_t>(node.text().as_ullong());
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, float& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(value);
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = node.text().as_float();
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, double& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(value);
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                value = node.text().as_double();
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, eastl::string& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(value.c_str());
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                const char* text = node.text().as_string();
                value = eastl::string(text);
            }
        }
    }

    void XmlArchive::Serialize(eastl::string_view name, PoolString& value) {
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.text().set(value.ToStringView().data());
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                const char* text = node.text().as_string();
                value = PoolString::Intern(eastl::string_view{text});
            }
        }
    }

    // =============================================================================
    // Attribute serialization implementations (XML attributes)
    // =============================================================================

    void XmlArchive::SerializeAttribute(eastl::string_view name, bool& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(value ? "true" : "false");
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                eastl::string_view text = attr.as_string();
                value = (text == "true" || text == "1");
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, int8_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(static_cast<int>(value));
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = static_cast<int8_t>(attr.as_int());
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, uint8_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(static_cast<unsigned int>(value));
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = static_cast<uint8_t>(attr.as_uint());
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, int16_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(static_cast<int>(value));
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = static_cast<int16_t>(attr.as_int());
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, uint16_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(static_cast<unsigned int>(value));
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = static_cast<uint16_t>(attr.as_uint());
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, int32_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(value);
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = attr.as_int();
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, uint32_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(value);
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = attr.as_uint();
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, int64_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(static_cast<long long>(value));
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = static_cast<int64_t>(attr.as_llong());
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, uint64_t& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(static_cast<unsigned long long>(value));
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = static_cast<uint64_t>(attr.as_ullong());
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, float& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(value);
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = attr.as_float();
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, double& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(value);
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                value = attr.as_double();
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, eastl::string& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(value.c_str());
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                const char* text = attr.as_string();
                value = eastl::string(text);
            }
        }
    }

    void XmlArchive::SerializeAttribute(eastl::string_view name, PoolString& value) {
        if (_mode == Mode::Write) {
            pugi::xml_attribute attr = GetOrCreateAttribute(name);
            attr.set_value(value.ToStringView().data());
        } else {
            pugi::xml_attribute attr = GetAttribute(name);
            if (attr) {
                const char* text = attr.as_string();
                value = PoolString::Intern(eastl::string_view{text});
            }
        }
    }

    // =============================================================================
    // Object nesting
    // =============================================================================

    bool XmlArchive::BeginObject(eastl::string_view name) {
        std::string nameStr(name.data(), name.size());
        
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            _nodeStack.push_back(node);
            return true;
        } else {
            pugi::xml_node node = GetChild(name);
            if (node) {
                _nodeStack.push_back(node);
                return true;
            }
            return false;
        }
    }

    void XmlArchive::EndObject() {
        if (_nodeStack.size() > 1) {
            _nodeStack.pop_back();
        }
    }

    // =============================================================================
    // Array support
    // =============================================================================

    bool XmlArchive::BeginArray(eastl::string_view name, size_t& count) {
        std::string nameStr(name.data(), name.size());
        
        if (_mode == Mode::Write) {
            pugi::xml_node node = GetOrCreateChild(name);
            node.append_attribute("count").set_value(static_cast<unsigned int>(count));
            _nodeStack.push_back(node);
            return true;
        } else {
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
    }

    void XmlArchive::EndArray() {
        if (!_arrayStack.empty()) {
            _arrayStack.pop_back();
        }
        if (_nodeStack.size() > 1) {
            _nodeStack.pop_back();
        }
    }

}  // namespace BECore
