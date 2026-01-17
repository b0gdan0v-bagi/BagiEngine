#pragma once

namespace BECore {

    class XmlNodeImpl : public RefCountedNonAtomic {
    public:
        using RawNode = std::variant<pugi::xml_node>;
        using RawIterator = std::variant<pugi::xml_node_iterator>;

        explicit XmlNodeImpl(pugi::xml_node node) : _node(node) {}

    private:
        template <typename F>
        auto Apply(F&& func) const {
            return std::visit(std::forward<F>(func), _node);
        }

    public:
        XmlNodeImpl* GetChild(std::string_view name) const {
            auto childNode = Apply([name](auto node) { return node.child(name.data()); });
            return new XmlNodeImpl(childNode);
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
                return EnumCast<T>(s);
            } else if constexpr (std::is_same_v<T, Math::Color>) {
                return Math::Color::ParseColorFromString(s);
            }

            return std::nullopt;
        }

        RawNode GetRawNode() const {
            return _node;
        }

    private:
        RawNode _node;
    };

}  // namespace BECore

