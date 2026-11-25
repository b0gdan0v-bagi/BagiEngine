#pragma once

#include <BECore/Reflection/FieldAttributes.h>
#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Shows one top-level field of an object to another visitor and nothing else
     *
     * Wraps @p inner for `AcceptPropertyVisitor`: the field whose serialized name is @p fieldName (a parent's field counts, as in a property
     * path) reaches `inner` with everything inside it (the children of an object, the elements and hooks of a list, an optional, a variant, a
     * `Value<T>`), in the order a full visit gives, and the other fields reach nothing. A skipped object or list answers `false` from
     * `BeginCompound` / `BeginList`, so the dispatch never visits its children. What `inner` returns comes back, so "changed" reaches the
     * caller. A name that is no field of the object gives no call at all.
     *
     * A field begins with `BeginField` and has no name until its first `Visit`, `VisitEnum`, `BeginCompound`, `BeginList` or `VisitPresence`,
     * so the attributes of the field are held (by value, no allocation) until that call says whether it is the one. `ShowsHidden()` is
     * `inner`'s, because the dispatch asks it before `BeginField`. `TextScratch()` is not virtual and is the decorator's own: a decorator that
     * lives across frames (a member of the widget) keeps its capacity, one made per frame allocates for a long text each time.
     */
    class SingleFieldVisitor final : public IPropertyVisitor {
    public:
        SingleFieldVisitor(IPropertyVisitor& inner, StringView fieldName) : _inner(inner), _fieldName(fieldName) {}

        /// The same decorator for another field (the name is not copied: it must outlive the visit)
        void SetFieldName(StringView fieldName) {
            _fieldName = fieldName;
        }

        /// Whether the last visit reached the field: false for a name the object does not have, or a hidden field
        [[nodiscard]] bool FoundField() const {
            return _found;
        }

        /// Forgets what the last visit saw; call before visiting another object
        void Reset();

        bool Visit(StringView name, bool& value) override;
        bool Visit(StringView name, int8_t& value) override;
        bool Visit(StringView name, uint8_t& value) override;
        bool Visit(StringView name, int16_t& value) override;
        bool Visit(StringView name, uint16_t& value) override;
        bool Visit(StringView name, int32_t& value) override;
        bool Visit(StringView name, uint32_t& value) override;
        bool Visit(StringView name, int64_t& value) override;
        bool Visit(StringView name, uint64_t& value) override;
        bool Visit(StringView name, float& value) override;
        bool Visit(StringView name, double& value) override;
        bool Visit(StringView name, PoolString& value) override;
        bool Visit(StringView name, String& value) override;
        bool VisitEnum(StringView name, const StringView* enumNames, size_t enumCount, size_t& selectedIndex) override;

        bool BeginCompound(StringView name) override;
        void EndCompound() override;
        void BeginField(const FieldAttributes& attributes) override;
        void EndField() override;
        bool BeginList(StringView name, const ListInfo& info) override;
        void EndListElement(size_t index, ListRequest& request) override;
        void EndList(ListRequest& request) override;
        void VisitNewKey(String& key) override;
        void VisitNewKey(int64_t& key) override;
        bool VisitPresence(StringView name, bool& present) override;
        bool VisitAlternative(StringView name, size_t count, size_t& index) override;
        void VisitValueField(StringView name, const ValueFieldInfo& info) override;
        bool ShowsHidden() const override;

    private:
        /// The first named call of a top-level field says which field it is; true when the call belongs to the field that is shown
        bool Admits(StringView name);

        IPropertyVisitor& _inner;
        StringView _fieldName;
        /// `BeginField` calls open now: 1 is a top-level field, more is inside one
        size_t _depth = 0;
        /// The top-level field is the shown one and its `BeginField` has been given to `inner`
        bool _shown = false;
        /// The top-level field has a name already (a `BeginField` of depth 1 resets it)
        bool _named = false;
        bool _found = false;
        FieldAttributes _held;
    };

}  // namespace BECore
