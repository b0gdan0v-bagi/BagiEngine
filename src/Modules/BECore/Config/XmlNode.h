#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <charconv>
#include <cstddef>
#include <iterator>
#include <optional>
#include <pugixml.hpp>
#include <type_traits>

namespace BECore {

    /// pugixml's `parent.child(name)` for a name that needs no NUL; an empty name finds nothing
    inline pugi::xml_node FindXmlChild(pugi::xml_node parent, StringView name) {
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

    /// pugixml's `node.attribute(name)` for a name that needs no NUL; an empty name finds nothing
    inline pugi::xml_attribute FindXmlAttribute(pugi::xml_node node, StringView name) {
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

    /**
     * @brief A copyable handle over a pugi::xml_node
     *
     * Owns nothing: it is valid only while its XmlDocument is alive and not reloaded.
     */
    class XmlNode {
    public:
        XmlNode() = default;
        explicit XmlNode(pugi::xml_node node) : _node(node) {}

        /// An empty node when there is no such child
        XmlNode GetChild(StringView name) const {
            return XmlNode{FindXmlChild(_node, name)};
        }

        bool IsEmpty() const {
            return _node.empty();
        }

        explicit operator bool() const {
            return !_node.empty();
        }

        bool operator!() const {
            return _node.empty();
        }

        /// Walks the element children only: comments, processing instructions and text are skipped
        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = XmlNode;
            using difference_type = std::ptrdiff_t;
            using pointer = XmlNode*;
            using reference = XmlNode;

            explicit Iterator(pugi::xml_node node) : _node(SkipToElement(node)) {}

            XmlNode operator*() const {
                return XmlNode(_node);
            }

            Iterator& operator++() {
                _node = SkipToElement(_node.next_sibling());
                return *this;
            }

            bool operator!=(const Iterator& other) const {
                return _node != other._node;
            }

        private:
            static pugi::xml_node SkipToElement(pugi::xml_node node) {
                while (node && node.type() != pugi::node_element) {
                    node = node.next_sibling();
                }
                return node;
            }

            pugi::xml_node _node;
        };

        struct ChildrenRange {
            pugi::xml_node parentNode;

            Iterator begin() const {
                return Iterator(parentNode.first_child());
            }

            Iterator end() const {
                return Iterator(pugi::xml_node());
            }
        };

        /// The child elements in document order; a config DOM may hold comments, readers never see them
        ChildrenRange Children() const {
            return ChildrenRange{_node};
        }

        StringView Name() const {
            return StringView(_node.name());
        }

        /// nullopt when the attribute is missing
        Optional<StringView> GetAttribute(StringView name) const {
            auto attr = FindXmlAttribute(_node, name);
            if (!attr) {
                return std::nullopt;
            }
            return StringView(attr.value());
        }

        /**
         * @brief The attribute parsed as T: integer, float, bool (`true`/`false`/`1`/`0`), StringView, enum or Color
         * @return nullopt when the attribute is missing or does not parse
         */
        template <typename T>
        Optional<T> ParseAttribute(StringView name) const {
            auto attrOpt = GetAttribute(name);

            if (!attrOpt)
                return std::nullopt;

            StringView s = *attrOpt;

            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                T result;
                auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
                if (ec == std::errc{}) {
                    return result;
                }
            } else if constexpr (std::is_floating_point_v<T>) {
                T result;
                auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
                if (ec == std::errc{})
                    return result;
            } else if constexpr (std::is_same_v<T, bool>) {
                if (s == "true" || s == "1")
                    return true;
                if (s == "false" || s == "0")
                    return false;
            } else if constexpr (std::is_same_v<T, StringView>) {
                return s;
            } else if constexpr (std::is_enum_v<T>) {
                return EnumUtils<T>::Cast(s);
            } else if constexpr (std::is_same_v<T, BECore::Color>) {
                return BECore::Color::ParseColorFromString(s);
            }

            return std::nullopt;
        }

        pugi::xml_node GetPugiNode() const {
            return _node;
        }

    private:
        pugi::xml_node _node;
    };

}  // namespace BECore
