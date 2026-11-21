#pragma once

#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/Singleton.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Explain/ProgramTrace.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Value.h>
#include <BECore/Values/ValueType.h>
#include <cstdint>
#include <cstring>

namespace BECore {

    /**
     * @brief Where the value of a `Value<T>` comes from, for one context
     *
     * `source` is what a person reads: the literal of a `Const`, the path of a `Slot`, the formula's text of a `Program`. `valueText` is the value now when `hasValue`; otherwise `why` says what is
     * missing. `trace` is empty for a `Const`, one `ContextKey` node for a `Slot` and the tree of `ExplainProgram` for a `Program`.
     */
    struct ValueExplanation {
        ValueKind kind = ValueKind::Const;
        String source;
        bool hasValue = false;
        String valueText;
        ProgramTrace trace;
        String why;
    };

    namespace Detail {

        /// The program type a `Value<T>` computes as, empty for a type no program computes (`Rect`, a game's type it did not register)
        template <ValueType T>
        [[nodiscard]] Optional<ProgramType> ProgramTypeOfValue() {
            if constexpr (ProgramValue<T>) {
                return kProgramTypeOf<T>;
            } else if (ProgramDomain::FindTypeByDescriptor(DescribeType<T>()).has_value()) {
                return ProgramType::Domain;
            } else {
                return std::nullopt;
            }
        }

        /// @p value as the register image a program holds it in; empty (the image untouched) when no program type fits
        template <ValueType T>
        [[nodiscard]] Optional<ProgramType> ToImage(const T& value, RegisterImage& image) {
            const Optional<ProgramType> type = ProgramTypeOfValue<T>();
            if (!type.has_value()) {
                return std::nullopt;
            }
            if constexpr (IsReflectedEnum<T>) {
                const ProgramEnum index{static_cast<uint32_t>(value)};
                std::memcpy(image.bytes.data(), &index, sizeof(index));
            } else {
                std::memcpy(image.bytes.data(), &value, sizeof(T));
            }
            return type;
        }

        /// `<Name N bytes: hex>`: a type the engine cannot print because nobody described it
        [[nodiscard]] String FormatOpaqueBytes(const TypeDesc& type, const void* bytes, size_t size);

        /// The name of the level the context lacks, for `why`; `level 3` when the declaration has no such level
        [[nodiscard]] String MissingLevelText(const ContextDecl& declaration, LevelIndex level);

        /// A one-node trace of a field read: the explanation of a `Slot`
        [[nodiscard]] ProgramTrace SlotTrace(StringView path, ProgramType type, const RegisterImage& image, const TypeDesc* described);

        /// Gives the enum or registered type of the value to a trace the program could not name it for (a program does not know which enum it computes)
        void NameResultType(ProgramTrace& trace, const TypeDesc& described);

    }  // namespace Detail

    /// @p value as text: a number as `Format("{}")`, an enum by its name, a `PoolString` in quotes, a registered type and any other by its bytes (`FormatImage`)
    template <ValueType T>
    [[nodiscard]] String FormatValue(const T& value) {
        RegisterImage image{};
        if (const Optional<ProgramType> type = Detail::ToImage(value, image)) {
            return FormatImage(*type, image, &DescribeType<T>());
        }
        return Detail::FormatOpaqueBytes(DescribeType<T>(), &value, sizeof(T));
    }

    /**
     * @brief Says what @p value is and, with @p context, what it is now and how it came to be
     *
     * Never asserts on a context that lacks an object at a level the value reads: `hasValue` is then false and `why` names the level. @p declaration names the fields the instructions of a program
     * read (`ExplainProgram`). Allocates; it runs when a person asks, never in a frame. A `Program` of a type no program computes (it cannot be made by `CompileValue`) has no value.
     */
    template <ValueType T>
    [[nodiscard]] ValueExplanation ExplainValue(const Value<T>& value, const Context& context, const ContextDecl& declaration) {
        ValueExplanation explanation;
        explanation.kind = value.Kind();
        switch (value.Kind()) {
            case ValueKind::Const:
                explanation.source = FormatValue(value.GetConst());
                explanation.hasValue = true;
                explanation.valueText = explanation.source;
                break;
            case ValueKind::Slot: {
                explanation.source = String(value.Path().ToStringView());
                if (context.Object(value.Level()) == nullptr) {
                    explanation.why = Detail::MissingLevelText(declaration, value.Level());
                    break;
                }
                const T& current = value.Get(context);
                explanation.hasValue = true;
                explanation.valueText = FormatValue(current);
                RegisterImage image{};
                if (const Optional<ProgramType> type = Detail::ToImage(current, image)) {
                    explanation.trace = Detail::SlotTrace(explanation.source, *type, image, &DescribeType<T>());
                }
                break;
            }
            case ValueKind::Program: {
                explanation.source = String(value.Path().ToStringView());
                const Program& program = ProgramPool::Get(value.Id());
                const Optional<ProgramType> expected = Detail::ProgramTypeOfValue<T>();
                if (!expected.has_value()) {
                    explanation.why = "no program computes this type";
                    break;
                }
                if (program.ResultType() != *expected) {
                    explanation.why = Format("the program computes {}, not the type of this value", EnumUtils<ProgramType>::ToString(program.ResultType()));
                    break;
                }
                explanation.trace = ExplainProgram(program, context, declaration);
                if (explanation.trace.missingLevel.has_value()) {
                    explanation.why = Detail::MissingLevelText(declaration, *explanation.trace.missingLevel);
                    break;
                }
                Detail::NameResultType(explanation.trace, DescribeType<T>());
                explanation.hasValue = true;
                explanation.valueText = FormatImage(explanation.trace.resultType, explanation.trace.result, explanation.trace.resultDescribed);
                break;
            }
        }
        return explanation;
    }

    /// Explains a `Value<T>` found by the identity of its type, for code that holds only the address of a field (the control API, the inspector)
    using ValueExplainFn = ValueExplanation (*)(const void* value, const Context& context, const ContextDecl& declaration);

    /**
     * @brief The `ExplainValue` of every `T` a game or the engine registered, keyed by the opaque hash of `DescribeType<Value<T>>()`
     *
     * The engine fills the program types and `Rect` (`RegisterBuiltinValueExplainers`); a game registers its own `Value<CellMask>` or a `Value<ItsEnum>` with `Register`. Main thread, but
     * locked: a registration and a lookup meet only when a person asks.
     */
    class ValueExplainers : public Singleton<ValueExplainers> {
    public:
        /// Registers the explanation of `Value<T>`; registering a type again changes nothing
        template <ValueType T>
        void Register() {
            const TypeDesc& type = DescribeType<Value<T>>();
            Add(type.opaqueHash, type.opaqueName,
                [](const void* value, const Context& context, const ContextDecl& declaration) { return ExplainValue(*static_cast<const Value<T>*>(value), context, declaration); });
        }

        /// The explanation of the `Value<T>` whose descriptor has @p opaqueHash, null when no such type was registered
        [[nodiscard]] ValueExplainFn Find(uint64_t opaqueHash) const;

        /// The names of the registered `Value<T>` types, sorted: what an error message tells a client who asked about another type
        [[nodiscard]] Vector<String> Names() const;

    private:
        struct Entry {
            ValueExplainFn explain = nullptr;
            StringView name;
        };

        void Add(uint64_t opaqueHash, StringView name, ValueExplainFn explain);

        mutable Mutex _mutex{BE_LOCK_SITE("ValueExplainers")};
        HashMap<uint64_t, Entry> _explainers;
    };

    /// Registers `Value<T>` for the numbers, `bool`, the vectors, `Quat`, `Color`, `PoolString` and `Rect`; an enum or a game's type is the game's to register
    void RegisterBuiltinValueExplainers();

}  // namespace BECore
