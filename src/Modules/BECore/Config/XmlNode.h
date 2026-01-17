#pragma once

#include <BECore/Config/XmlNodeImpl.h>

namespace BECore {

    class XmlNode {

    public:
        XmlNode() = default;
        explicit XmlNode(pugi::xml_node node) : _impl(MakeIntrusiveNonAtomic(new XmlNodeImpl(node))) {}

        XmlNode GetChild(std::string_view name) const {
            if (!_impl) {
                return {};
            }
            return XmlNode(MakeIntrusiveNonAtomic(_impl->GetChild(name)));
        }

        bool IsEmpty() const {
            if (!_impl) {
                return true;
            }
            return _impl->IsEmpty();
        }

        explicit operator bool() const {
            if (!_impl) {
                return false;
            }
            return static_cast<bool>(*_impl);
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

            Iterator(const XmlNodeImpl::RawIterator& it) : _it(it) {}

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
            XmlNodeImpl::RawIterator _it;
        };

        struct ChildrenRange {
            XmlNodeImpl::RawNode parentNode;

            Iterator begin() const {
                return std::visit(overload{[](pugi::xml_node node) { return Iterator(node.begin()); }}, parentNode);
            }

            Iterator end() const {
                return std::visit(overload{[](pugi::xml_node node) { return Iterator(node.end()); }}, parentNode);
            }
        };

        ChildrenRange Children() const {
            if (!_impl) {
                return ChildrenRange{pugi::xml_node{}};
            }
            return ChildrenRange{_impl->GetRawNode()};
        }

        std::string_view Name() const {
            if (!_impl) {
                return {};
            }
            return _impl->Name();
        }

        std::optional<std::string_view> GetAttribute(std::string_view name) const {
            if (!_impl) {
                return std::nullopt;
            }
            return _impl->GetAttribute(name);
        }

        template <typename T>
        std::optional<T> ParseAttribute(std::string_view name) const {
            if (!_impl) {
                return std::nullopt;
            }
            return _impl->ParseAttribute<T>(name);
        }

    private:
        explicit XmlNode(IntrusivePtrNonAtomic<XmlNodeImpl> impl) : _impl(std::move(impl)) {}

        IntrusivePtrNonAtomic<XmlNodeImpl> _impl;

        friend class XmlConfigImpl;
    };

}  // namespace BECore
