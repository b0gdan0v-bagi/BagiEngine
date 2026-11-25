#include "SingleFieldVisitor.h"

namespace BECore {

    void SingleFieldVisitor::Reset() {
        _depth = 0;
        _shown = false;
        _named = false;
        _found = false;
    }

    bool SingleFieldVisitor::Admits(StringView name) {
        if (_depth == 1 && !_named) {
            _named = true;
            if (name == _fieldName) {
                _shown = true;
                _found = true;
                _inner.BeginField(_held);
            }
        }
        return _shown;
    }

    void SingleFieldVisitor::BeginField(const FieldAttributes& attributes) {
        ++_depth;
        if (_depth == 1) {
            _held = attributes;
            _named = false;
            _shown = false;
        } else if (_shown) {
            _inner.BeginField(attributes);
        }
    }

    void SingleFieldVisitor::EndField() {
        if (_shown) {
            _inner.EndField();
        }
        if (_depth > 0) {
            --_depth;
        }
        if (_depth == 0) {
            _shown = false;
        }
    }

    bool SingleFieldVisitor::Visit(StringView name, bool& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, int8_t& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, uint8_t& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, int16_t& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, uint16_t& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, int32_t& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, uint32_t& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, int64_t& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, uint64_t& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, float& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, double& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, PoolString& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::Visit(StringView name, String& value) {
        return Admits(name) && _inner.Visit(name, value);
    }

    bool SingleFieldVisitor::VisitEnum(StringView name, const StringView* enumNames, size_t enumCount, size_t& selectedIndex) {
        return Admits(name) && _inner.VisitEnum(name, enumNames, enumCount, selectedIndex);
    }

    bool SingleFieldVisitor::BeginCompound(StringView name) {
        return Admits(name) && _inner.BeginCompound(name);
    }

    void SingleFieldVisitor::EndCompound() {
        _inner.EndCompound();
    }

    bool SingleFieldVisitor::BeginList(StringView name, const ListInfo& info) {
        return Admits(name) && _inner.BeginList(name, info);
    }

    void SingleFieldVisitor::EndListElement(size_t index, ListRequest& request) {
        _inner.EndListElement(index, request);
    }

    void SingleFieldVisitor::EndList(ListRequest& request) {
        _inner.EndList(request);
    }

    void SingleFieldVisitor::VisitNewKey(String& key) {
        _inner.VisitNewKey(key);
    }

    void SingleFieldVisitor::VisitNewKey(int64_t& key) {
        _inner.VisitNewKey(key);
    }

    bool SingleFieldVisitor::VisitPresence(StringView name, bool& present) {
        return Admits(name) && _inner.VisitPresence(name, present);
    }

    bool SingleFieldVisitor::VisitAlternative(StringView name, size_t count, size_t& index) {
        return Admits(name) && _inner.VisitAlternative(name, count, index);
    }

    void SingleFieldVisitor::VisitValueField(StringView name, const ValueFieldInfo& info) {
        if (Admits(name)) {
            _inner.VisitValueField(name, info);
        }
    }

    bool SingleFieldVisitor::ShowsHidden() const {
        return _inner.ShowsHidden();
    }

}  // namespace BECore
