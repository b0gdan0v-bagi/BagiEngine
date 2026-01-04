#pragma once

#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <pugixml.hpp>
#include <string>
#include <variant>
#include <Color.h>

namespace Core {

    template <class... Ts>
    struct overload : Ts... {
        using Ts::operator()...;
    };

    class XmlNode {

        using RawNode = std::variant<pugi::xml_node>;
        using RawIterator = std::variant<pugi::xml_node_iterator>;

    public:
        XmlNode() = default;
        explicit XmlNode(pugi::xml_node node) : _node(node) {}

        XmlNode GetChild(std::string_view name) const {
            return Apply([name](auto node) { return XmlNode(node.child(name.data())); });
        }

        bool IsEmpty() const {
            return std::visit(overload{[](pugi::xml_node node) { return node.empty(); }}, _node);
        }

        explicit operator bool() const {
            return std::visit(overload{[](pugi::xml_node node) { return !node.empty(); }}, _node);
        }

        bool operator!() const {
            return !static_cast<bool>(*this);
        }

        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = XmlNode;
            using difference_type = std::ptrdiff_t;
            using pointer = XmlNode*;
            using reference = XmlNode;

            Iterator(const RawIterator& it) : _it(it) {}

            // Разыменование возвращает нашу обертку XmlNode
            XmlNode operator*() const {
                return std::visit(overload{[](pugi::xml_node_iterator it) { return XmlNode(*it); }}, _it);
            }

            Iterator& operator++() {
                std::visit(overload{[](pugi::xml_node_iterator& it) { ++it; }}, _it);
                return *this;
            }

            bool operator!=(const Iterator& other) const {
                return _it != other._it;
            }

        private:
            RawIterator _it;
        };

        struct ChildrenRange {
            RawNode parentNode;

            Iterator begin() const {
                return std::visit(overload{[](pugi::xml_node node) { return Iterator(node.begin()); }}, parentNode);
            }

            Iterator end() const {
                return std::visit(overload{[](pugi::xml_node node) { return Iterator(node.end()); }}, parentNode);
            }
        };

        ChildrenRange Children() const {
            return ChildrenRange{_node};
        }

        std::string_view Name() const {
            return std::visit(overload{[](pugi::xml_node node) { return std::string_view(node.name()); }}, _node);
        }

        std::optional<std::string_view> GetAttribute(std::string_view name) const {
            return std::visit(overload{[name](pugi::xml_node node) -> std::optional<std::string_view> {
                                  auto attr = node.attribute(name.data());
                                  if (!attr) {
                                      return std::nullopt;
                                  }
                                  return std::string_view(attr.value());
                              }},
                              _node);
        }

        template <typename T>
        std::optional<T> ParseAttribute(std::string_view name) const {
            auto attrOpt = GetAttribute(name);

            if (!attrOpt)
                return std::nullopt;

            std::string_view s = *attrOpt;

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
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                return s;
            } else if constexpr (std::is_enum_v<T>) {
                return magic_enum::enum_cast<T>(s);
            } else if constexpr (std::is_same_v<T, Math::Color>) {
                return Math::Color::ParseColorFromString(s);
            }

            return std::nullopt;
        }

    private:
        template <typename F>
        auto Apply(F&& func) const {
            return std::visit(std::forward<F>(func), _node);
        }

        RawNode _node;
    };

}  // namespace Core

