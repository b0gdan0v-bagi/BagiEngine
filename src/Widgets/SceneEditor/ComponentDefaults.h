#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    class IComponent;

    /// A top-level field of a component that differs from its default
    struct ComponentFieldDifference {
        /// The serialized name of the field
        StringView field;
        /// The default as one line of text; it lives as long as the `ComponentDefaults` that answered
        StringView defaultText;
        /// False for a `readonly` field and for one that cannot be loaded: `Reset` refuses it
        bool resettable = true;
    };

    /**
     * @brief What a component is by default, compared and restored field by field
     *
     * The default of a type is a component `AbstractFactory<IComponent>` makes (not initialized, not attached: the state the loader reads a file into, so
     * "differs" here is what Save writes). One prototype per type is made at the first question and dies with this object, which is why it is a member of
     * its user and no global: a game's types must not outlive the game. A type the factory cannot make has no default and nothing of it differs.
     *
     * A field differs when what its `FieldDesc::save` writes is not what the default's writes. A `hidden` field and a field that holds object pointers at any
     * depth (the elements of a canvas) are never compared or reset: a reset would delete the objects.
     *
     * A comparison of a type seen before allocates nothing, unless the save of a field itself does (a hash map sorts its keys).
     */
    class ComponentDefaults {
    public:
        ComponentDefaults();
        ~ComponentDefaults();
        ComponentDefaults(const ComponentDefaults&) = delete;
        ComponentDefaults& operator=(const ComponentDefaults&) = delete;

        /// The fields of @p component that differ, the parents' first; the span is valid until the next call of `Compare` or of a reset
        Span<const ComponentFieldDifference> Compare(IComponent& component);

        /// The default of the field of this serialized name as one line of text, whether it differs or not; empty for a field that is never compared.
        /// It lives as long as this object and leaves the span of `Compare` alone
        Optional<StringView> DefaultText(IComponent& component, StringView field);

        /**
         * @brief Gives the field of this serialized name its default and calls `IComponent::OnPropertyWritten` for it; no other field is written here
         *
         * False, with nothing done, for a field that does not differ, is not resettable or is unknown, and when the default cannot be read back.
         */
        bool Reset(IComponent& component, StringView field);

        /// Resets every differing field that can be: all values first, then the `OnPropertyWritten` calls, so a hook sees the others at their defaults. True when any was
        bool ResetAll(IComponent& component);

    private:
        struct Prototype;
        struct Field;

        const Prototype& PrototypeOf(IComponent& component);

        /// By `ClassMeta::typeHash`; held by pointer because the differences view the texts of a prototype
        HashMap<uint64_t, UniquePtr<Prototype>> _prototypes;
        Vector<ComponentFieldDifference> _differences;
    };

}  // namespace BECore
