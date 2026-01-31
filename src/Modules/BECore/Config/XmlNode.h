#pragma once

#include <pugixml.hpp>
#include <EASTL/string_view.h>
#include <optional>
#include <charconv>

namespace BECore {

    /**
     * @brief Lightweight wrapper for pugi::xml_node
     * 
     * Value type (8-16 bytes) that wraps pugi::xml_node handle.
     * Does NOT own the underlying XML data - lifetime tied to the XmlDocument.
     * Cheap to copy and pass by value.
     */
    class XmlNode {
    public:
        XmlNode() = default;
        explicit XmlNode(pugi::xml_node node) : _node(node) {}

        /**
         * @brief Get child node by name
         * @param name Name of the child element
         * @return XmlNode wrapping the child, or empty node if not found
         */
        XmlNode GetChild(eastl::string_view name) const {
            return XmlNode{_node.child(name.data())};
        }

        /**
         * @brief Check if node is empty
         * @return True if node is empty/invalid
         */
        bool IsEmpty() const {
            return _node.empty();
        }

        /**
         * @brief Check if node is valid
         * @return True if node has content
         */
        explicit operator bool() const {
            return !_node.empty();
        }

        /**
         * @brief Inverse validity check
         */
        bool operator!() const {
            return _node.empty();
        }

        /**
         * @brief Iterator for child nodes
         */
        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = XmlNode;
            using difference_type = std::ptrdiff_t;
            using pointer = XmlNode*;
            using reference = XmlNode;

            Iterator(pugi::xml_node_iterator it) : _it(it) {}

            XmlNode operator*() const {
                return XmlNode(*_it);
            }

            Iterator& operator++() {
                ++_it;
                return *this;
            }

            bool operator!=(const Iterator& other) const {
                return _it != other._it;
            }

        private:
            pugi::xml_node_iterator _it;
        };

        /**
         * @brief Range for iterating child nodes
         */
        struct ChildrenRange {
            pugi::xml_node parentNode;

            Iterator begin() const {
                return Iterator(parentNode.begin());
            }

            Iterator end() const {
                return Iterator(parentNode.end());
            }
        };

        /**
         * @brief Get range of all child nodes
         * @return ChildrenRange for range-based for loop
         */
        ChildrenRange Children() const {
            return ChildrenRange{_node};
        }

        /**
         * @brief Get node name
         * @return Name of the element
         */
        eastl::string_view Name() const {
            return eastl::string_view(_node.name());
        }

        /**
         * @brief Get attribute value as string
         * @param name Attribute name
         * @return Attribute value or nullopt if not found
         */
        std::optional<eastl::string_view> GetAttribute(eastl::string_view name) const {
            auto attr = _node.attribute(name.data());
            if (!attr) {
                return std::nullopt;
            }
            return eastl::string_view(attr.value());
        }

        /**
         * @brief Parse attribute as specific type
         * @tparam T Type to parse (int, float, bool, string_view, enum, Color)
         * @param name Attribute name
         * @return Parsed value or nullopt if not found or parse failed
         */
        template <typename T>
        std::optional<T> ParseAttribute(eastl::string_view name) const {
            auto attrOpt = GetAttribute(name);

            if (!attrOpt)
                return std::nullopt;

            eastl::string_view s = *attrOpt;

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
            } else if constexpr (std::is_same_v<T, eastl::string_view>) {
                return s;
            } else if constexpr (std::is_enum_v<T>) {
                return EnumUtils<T>::Cast(s);
            } else if constexpr (std::is_same_v<T, BECore::Color>) {
                return BECore::Color::ParseColorFromString(s);
            }

            return std::nullopt;
        }

        /**
         * @brief Get underlying pugi::xml_node for internal use
         * @return pugi::xml_node handle
         */
        pugi::xml_node GetPugiNode() const {
            return _node;
        }

    private:
        pugi::xml_node _node;
    };

}  // namespace BECore
