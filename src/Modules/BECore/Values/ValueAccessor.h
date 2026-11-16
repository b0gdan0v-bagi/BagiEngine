#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Values/Value.h>
#include <BECore/Values/ValueLinkScope.h>

namespace BECore::Ser {

    namespace Detail {

        inline constexpr StringView kValueFromKey = "from";
        inline constexpr StringView kValueLiteralKey = "value";
        inline constexpr StringView kValueKindKey = "kind";

        /// A type whose accessor is one attribute (`damage="12"`), against an object or an opaque type whose accessor is a child element
        template <typename T>
        concept IsAttributeValue = std::is_arithmetic_v<T> || std::is_same_v<T, PoolString> || IsReflectedEnum<T>;

        /// What the optional `kind` entry of the object form says; a binary writer that wants an explicit tag writes it, a text file leaves it out
        CORE_ENUM(ValueForm, uint8_t, Unspecified, Const, Slot)

        /// False after an error: `Program` has no spelling until formulas exist, and any other text is no kind
        inline bool ReadValueForm(IDeserializer& d, ValueForm& form) {
            String text;
            if (!d.ReadAttribute(kValueKindKey, text)) {
                return true;
            }
            if (text == "Const") {
                form = ValueForm::Const;
            } else if (text == "Slot") {
                form = ValueForm::Slot;
            } else if (text == "Program") {
                d.ReportError(kValueKindKey, "A program value cannot be loaded yet: formulas arrive with data-values-programs");
                return false;
            } else {
                d.ReportError(kValueKindKey, Format("Unknown value kind '{}' (Const or Slot)", text));
                return false;
            }
            return true;
        }

        /// The `from` of an open object scope linked against the scope of the load; the value stays as it was on an error
        template <typename T>
        bool LoadValueSlot(IDeserializer& d, Value<T>& value, ValueForm form) {
            if (form == ValueForm::Const) {
                d.ReportError(kValueFromKey, "A value of kind Const has a literal, not a path to read");
                return false;
            }
            if (d.HasKey(kValueLiteralKey)) {
                d.ReportError(kValueLiteralKey, "A value is a literal ('value') or a read of the context ('from'), not both");
                return false;
            }
            String path;
            if (!d.ReadAttribute(kValueFromKey, path)) {
                // A key that is there and failed to read was reported by the reader
                if (!d.HasKey(kValueFromKey)) {
                    d.ReportError(kValueFromKey, "A value of kind Slot needs the path to read in 'from'");
                }
                return false;
            }
            const ContextDecl* declaration = ValueLinkScope::Current();
            if (declaration == nullptr) {
                d.ReportError(kValueFromKey, Format("No context is declared where this value is read, so the path '{}' names nothing", path));
                return false;
            }
            Value<T> linked;
            if (const auto result = linked.Link(*declaration, path); !result.has_value()) {
                d.ReportError(kValueFromKey, result.error().message);
                return false;
            }
            value = linked;
            return true;
        }

        /// A literal in the object form `<key value="12"/>`, for the types that are one attribute
        template <typename T>
        bool LoadValueLiteral(IDeserializer& d, Value<T>& value) {
            T literal{};
            if (!DataAccessor<T>::Load(d, literal, kValueLiteralKey)) {
                if (!d.HasKey(kValueLiteralKey)) {
                    d.ReportError(kValueLiteralKey, "A value needs 'value' (a literal) or 'from' (a path into the context)");
                }
                return false;
            }
            value = Value<T>::Const(literal);
            return true;
        }

    }  // namespace Detail

    /**
     * @brief A `Value<T>` is `damage="12"`, `<damage value="12"/>` (a literal) or `<damage from="caster.attack"/>` (a read of the context)
     *
     * An object or opaque `T` keeps its own form as the literal (`<pos x=".." y=".." z=".."/>`) and `<pos from="caster.position"/>` reads.
     * A literal of one attribute saves as that attribute, so a file written before the field became a `Value` saves as it was. The object
     * form may carry `kind` (`Const` or `Slot`); `Program` is refused until formulas exist. A `from` links against the `ValueLinkScope` of
     * the caller of the load. A load that fails leaves the field as it was, with the error under the path of the field.
     */
    template <typename T>
    struct DataAccessor<Value<T>> {
        static void Save(ISerializer& s, const Value<T>& value, StringView key) {
            switch (value.Kind()) {
                case ValueKind::Const:
                    DataAccessor<T>::Save(s, value.GetConst(), key);
                    break;
                case ValueKind::Slot:
                    if (s.BeginObject(key)) {
                        s.WriteAttribute(Detail::kValueFromKey, value.Path());
                        s.EndObject();
                    }
                    break;
                case ValueKind::Program:
                    ASSERT(false, "A program value has no saved form yet");
                    break;
            }
        }

        static bool Load(IDeserializer& d, Value<T>& value, StringView key) {
            // A reader strict about shapes (JSON) refuses an object scope around `"damage": 12`: that member is the plain literal
            if (!d.IsScalarMember(key) && d.BeginObject(key)) {
                Detail::ValueForm form = Detail::ValueForm::Unspecified;
                if (!Detail::ReadValueForm(d, form)) {
                    d.SkipScope();
                    d.EndObject();
                    return false;
                }
                if (d.HasKey(Detail::kValueFromKey) || form == Detail::ValueForm::Slot) {
                    const bool loaded = Detail::LoadValueSlot(d, value, form);
                    d.EndObject();
                    return loaded;
                }
                if constexpr (Detail::IsAttributeValue<T>) {
                    const bool loaded = Detail::LoadValueLiteral(d, value);
                    d.EndObject();
                    return loaded;
                } else {
                    // The literal of an object is its own element: leave this one unchecked and read that
                    d.SkipScope();
                    d.EndObject();
                }
            }
            T literal{};
            if (!DataAccessor<T>::Load(d, literal, key)) {
                return false;
            }
            value = Value<T>::Const(literal);
            return true;
        }
    };

}  // namespace BECore::Ser
