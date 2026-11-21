#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Values/Nodes/CalcNode.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <BECore/Values/ValueLinkScope.h>

namespace BECore::Ser {

    namespace Detail {

        inline constexpr StringView kValueFromKey = "from";
        inline constexpr StringView kValueLiteralKey = "value";
        inline constexpr StringView kValueKindKey = "kind";
        inline constexpr StringView kValueExprKey = "expr";
        inline constexpr StringView kValueTreeKey = "tree";

        /// A type whose accessor is one attribute (`damage="12"`), against an object or an opaque type whose accessor is a child element
        template <typename T>
        concept IsAttributeValue = std::is_arithmetic_v<T> || std::is_same_v<T, PoolString> || IsReflectedEnum<T>;

        /// What the optional `kind` entry of the object form says; a binary writer that wants an explicit tag writes it, a text file leaves it out
        CORE_ENUM(ValueForm, uint8_t, Unspecified, Const, Slot, Program)

        /// False after an error: any text but `Const`, `Slot` and `Program` is no kind
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
                form = ValueForm::Program;
            } else {
                d.ReportError(kValueKindKey, Format("Unknown value kind '{}' (Const, Slot or Program)", text));
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
                d.ReportError(kValueFromKey, ValueLinkScope::AtDocument(Format("No context is declared where this value is read, so the path '{}' names nothing", path)));
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

        /// The error as one line for a report: the column first, because a reader cuts a long message and the column must survive it
        inline String DescribeProgramError(const ProgramError& error) {
            return error.column == 0 ? error.message : Format("column {}: {}", error.column, error.message);
        }

        /// The formula @p text compiled against the scope of the load into @p value, which stays as it was on an error. A report is under @p key and is the compiler's message with
        /// its column, ending with the document's name. @p printed is a tree's text: the columns are its, so the report carries it
        template <typename T>
        bool CompileLoadedFormula(IDeserializer& d, Value<T>& value, StringView key, StringView text, bool printed) {
            const ContextDecl* declaration = ValueLinkScope::Current();
            if (declaration == nullptr) {
                d.ReportError(key, ValueLinkScope::AtDocument(Format("No context is declared where this value is read, so the formula '{}' names nothing", text)));
                return false;
            }
            CompileOptions options;
            options.tables = ValueLinkScope::Tables();
            const auto compiled = CompileValue<T>(text, *declaration, options);
            if (!compiled.has_value()) {
                String message = DescribeProgramError(compiled.error());
                if (printed) {
                    AppendFormat(message, " (the tree prints as '{}')", text);
                }
                // A tree's text comes after the message and the reader's cut would take the name: only a formula's own message is shortened for it
                d.ReportError(key, printed ? ValueLinkScope::AtDocument(message) : ValueLinkScope::AtDocumentWithin(message));
                return false;
            }
            value = *compiled;
            return true;
        }

        /// The formula of an open object scope: the `expr` text, or the `tree` that is built into its canonical text first (`hasTree`). Only one of `expr`, `tree`, `from` and `value` may be
        /// there, and a tree that cannot be built or read is reported under `tree` before any text is compiled
        template <typename T>
        bool LoadValueProgram(IDeserializer& d, Value<T>& value, ValueForm form, bool hasTree) {
            if (form == ValueForm::Const || form == ValueForm::Slot) {
                d.ReportError(hasTree ? kValueTreeKey : kValueExprKey,
                              Format("A value of kind {} has {}, not a formula", EnumUtils<ValueForm>::ToString(form), form == ValueForm::Const ? "a literal" : "a path to read"));
                return false;
            }
            const StringView own = hasTree ? kValueTreeKey : kValueExprKey;
            String text;
            if (!hasTree && !d.ReadAttribute(kValueExprKey, text)) {
                if (!d.HasKey(kValueExprKey)) {
                    d.ReportError(kValueExprKey, "A value of kind Program needs the formula in 'expr' or a 'tree'");
                }
                return false;
            }
            for (const StringView other : {kValueExprKey, kValueFromKey, kValueLiteralKey}) {
                if (other != own && d.HasKey(other)) {
                    d.ReportError(
                        other,
                        Format("A value is a literal ('value'), a read of the context ('from'), a formula ('expr') or a tree ('tree'): only one of them, and '{}' is there beside '{}'", other, own));
                    return false;
                }
            }
            if (hasTree) {
                return ReadFormulaTree(d, kValueTreeKey, text) && CompileLoadedFormula(d, value, kValueTreeKey, text, true);
            }
            return CompileLoadedFormula(d, value, kValueExprKey, text, false);
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
     * @brief A `Value<T>` is `damage="12"`, `<damage value="12"/>` (a literal), `<damage from="caster.attack"/>` (a read of the context) or
     *        `<damage expr="caster.attack * 2"/>` (a formula, compiled against the declaration of the scope as `from` is linked)
     *
     * An object or opaque `T` keeps its own form as the literal (`<pos x=".." y=".." z=".."/>`) and `<pos from="caster.position"/>` reads.
     * A literal of one attribute saves as that attribute, so a file written before the field became a `Value` saves as it was. A formula that
     * folds to a constant or to one field loads as that `Const` or `Slot` and saves as such; a program saves as its text. The object form may carry `kind` (`Const`,
     * `Slot` or `Program`); the keys `kind`, `from`, `value` and `expr` are reserved, and only one of the last three may be there. `expr` is looked for before the literal of an object type, so `<aim
     * expr=".."/>` of a `Value<Vec3>` is never read as the element form of the literal. `expr` on a type no program computes is the error that lists the types that can. A `from` links against the
     * declaration of the `ValueLinkScope` of the load (`ContextDecl` in `ContextDeclAccessor.h` declares it from the document). A load that fails leaves the field as it was, with the error under the
     * path of the field.
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
                    if (s.BeginObject(key)) {
                        s.WriteAttribute(Detail::kValueExprKey, value.Path());
                        s.EndObject();
                    }
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
                // Asked first and always, so `expr` and `tree` are known keys of the strict mode and the diagnostics whatever form the entry has
                const bool hasTree = d.HasKey(Detail::kValueTreeKey);
                if (hasTree || d.HasKey(Detail::kValueExprKey) || form == Detail::ValueForm::Program) {
                    const bool loaded = Detail::LoadValueProgram(d, value, form, hasTree);
                    d.EndObject();
                    return loaded;
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
