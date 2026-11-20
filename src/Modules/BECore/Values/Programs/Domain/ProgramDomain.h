#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/ValueType.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <type_traits>
#include <utility>

namespace BECore {

    /// The place of a game's value type in the registry; never reused, so a program that names it stays meaningful
    using DomainTypeId = uint32_t;

    /// The place of a game's operation (one overload of one name) in the registry
    using DomainOperationId = uint32_t;

    inline constexpr DomainTypeId kNoDomainType = std::numeric_limits<DomainTypeId>::max();

    /// An operation reads its arguments from the three register fields of its instruction
    inline constexpr size_t kMaxDomainArguments = 3;

    /// What `==` of a registered type means. `Bitwise` compares the bytes, which is right only for a type with unique object representations (integers: no padding, no float);
    /// `None` makes `==` a load error, and the game registers an operation that compares
    CORE_ENUM(DomainEquality, uint8_t, Bitwise, None)

    struct DomainTypeOptions {
        /// Empty: `Bitwise` for a type with unique object representations, else `None`
        Optional<DomainEquality> equality;
    };

    /// A registered value type: what the checker finds a context field's type by (`descriptor`) and what a message calls it (`name`)
    struct DomainType {
        PoolString name;
        const TypeDesc* descriptor = nullptr;
        uint32_t size = 0;
        DomainEquality equality = DomainEquality::None;
    };

    /**
     * @brief The type of one argument or result of an operation: a program type, an enum or a registered type
     *
     * `domain` names the registered type (`kNoDomainType` for the others); `enumType` is the descriptor of the enum when the program type is `Enum`, since the register holds only
     * the index. `type` says nothing for a registered type (`IsDomain()`: the program type of those comes with the instruction set, `ProgramType::Domain`). Two values are the same type when these agree.
     */
    struct DomainValueType {
        ProgramType type = ProgramType::Int32;
        DomainTypeId domain = kNoDomainType;
        const TypeDesc* enumType = nullptr;

        [[nodiscard]] bool IsDomain() const {
            return domain != kNoDomainType;
        }

        [[nodiscard]] bool operator==(const DomainValueType& other) const {
            const bool sameEnum = enumType == other.enumType || (enumType != nullptr && other.enumType != nullptr && SameType(*enumType, *other.enumType));
            return type == other.type && domain == other.domain && sameEnum;
        }
    };

    /// The raw form of an operation: reads its arguments from `ins.a`, `ins.b`, `ins.c`, writes the result to `ins.dst`. A plain function pointer: the interpreter calls it from one `case`
    using DomainOperationFn = void (*)(RegisterImage* regs, const Instruction& ins, const Context& context);

    /// A registered operation: one overload of a name
    struct DomainOperation {
        PoolString name;
        Array<DomainValueType, kMaxDomainArguments> parameters{};
        uint8_t parameterCount = 0;
        DomainValueType result;
        /// The function takes `const Context&` first: it reads the objects of the context
        bool takesContext = false;
        DomainOperationFn function = nullptr;
    };

    namespace Detail {

        /// A C++ type that is an argument or the result of an operation: what a program computes with, or a type a game may register
        template <typename T>
        concept DomainOperand = ProgramValue<T> || ValueType<T>;

        template <typename... A>
        struct DomainTypeList {};

        template <typename F>
        struct DomainFunctionTraits {
            static constexpr bool kIsFunction = false;
        };

        template <typename R, typename... A>
        struct DomainFunctionTraits<R (*)(A...)> {
            static constexpr bool kIsFunction = true;
            using Result = R;
            using Arguments = DomainTypeList<A...>;
        };

        template <typename R, typename... A>
        struct DomainFunctionTraits<R (*)(A...) noexcept> : DomainFunctionTraits<R (*)(A...)> {};

        template <typename A>
        inline constexpr bool kIsContextArgument = std::is_lvalue_reference_v<A> && std::is_const_v<std::remove_reference_t<A>> && std::is_same_v<std::remove_cvref_t<A>, Context>;

        /// The arguments of an operation without its optional leading `const Context&`
        template <typename List>
        struct StripContext;

        template <>
        struct StripContext<DomainTypeList<>> {
            static constexpr bool kTakesContext = false;
            using Parameters = DomainTypeList<>;
        };

        template <typename First, typename... Rest>
        struct StripContext<DomainTypeList<First, Rest...>> {
            static constexpr bool kTakesContext = kIsContextArgument<First>;
            using Parameters = std::conditional_t<kTakesContext, DomainTypeList<Rest...>, DomainTypeList<First, Rest...>>;
        };

        template <typename List>
        struct AllDomainOperands;

        template <typename... P>
        struct AllDomainOperands<DomainTypeList<P...>> {
            static constexpr bool kValue = (DomainOperand<std::remove_cvref_t<P>> && ...);
        };

        template <typename T>
        [[nodiscard]] T LoadOperand(const RegisterImage& reg) {
            if constexpr (IsReflectedEnum<T>) {
                ProgramEnum index;
                std::memcpy(&index, reg.bytes.data(), sizeof(index));
                return static_cast<T>(index.index);
            } else {
                T value{};
                std::memcpy(&value, reg.bytes.data(), sizeof(T));
                return value;
            }
        }

        /// A registered type is stored byte for byte and the rest of the register is zero, so equal values are equal images (`Eq` and `Select` of a domain type read all of it)
        template <typename T>
        void StoreOperand(RegisterImage& reg, const T& value) {
            if constexpr (IsReflectedEnum<T>) {
                const ProgramEnum index{static_cast<uint32_t>(value)};
                std::memcpy(reg.bytes.data(), &index, sizeof(index));
            } else if constexpr (ProgramValue<T>) {
                std::memcpy(reg.bytes.data(), &value, sizeof(T));
            } else {
                reg = RegisterImage{};
                std::memcpy(reg.bytes.data(), &value, sizeof(T));
            }
        }

        template <auto Function, bool TakesContext, typename... Parameters, size_t... Indexes>
        void CallOperation(RegisterImage* regs, const Instruction& ins, const Context& context, DomainTypeList<Parameters...>, std::index_sequence<Indexes...>) {
            const Array<uint8_t, kMaxDomainArguments> registers{ins.a, ins.b, ins.c};
            if constexpr (TakesContext) {
                StoreOperand(regs[ins.dst], Function(context, LoadOperand<std::remove_cvref_t<Parameters>>(regs[registers[Indexes]])...));
            } else {
                StoreOperand(regs[ins.dst], Function(LoadOperand<std::remove_cvref_t<Parameters>>(regs[registers[Indexes]])...));
            }
        }

        /// The raw function of @p Function: loads each argument from its register as its own type, calls it, stores the result
        template <auto Function, bool TakesContext, typename... Parameters>
        void RawOperation(RegisterImage* regs, const Instruction& ins, const Context& context) {
            static_assert(sizeof...(Parameters) <= kMaxDomainArguments);
            CallOperation<Function, TakesContext>(regs, ins, context, DomainTypeList<Parameters...>{}, std::make_index_sequence<sizeof...(Parameters)>{});
        }

        template <auto Function, bool TakesContext, typename... Parameters>
        [[nodiscard]] constexpr DomainOperationFn RawOperationOf(DomainTypeList<Parameters...>) {
            if constexpr (sizeof...(Parameters) <= kMaxDomainArguments) {
                return &RawOperation<Function, TakesContext, Parameters...>;
            } else {
                return nullptr;
            }
        }

    }  // namespace Detail

    /// A C++ type a game may register as a value type: a `Value<T>` accepts it and no program computes it already (a reflected enum and the engine's types are program types)
    template <typename T>
    concept DomainValueClass = ValueType<T> && !ProgramValue<T>;

    /// A function `AddOperation` accepts: a plain function whose parameters and result are program types or value types, an optional first `const Context&`
    template <auto Function>
    concept DomainFunction = Detail::DomainFunctionTraits<decltype(Function)>::kIsFunction && !std::is_void_v<typename Detail::DomainFunctionTraits<decltype(Function)>::Result> &&
                             Detail::DomainOperand<std::remove_cvref_t<typename Detail::DomainFunctionTraits<decltype(Function)>::Result>> &&
                             Detail::AllDomainOperands<typename Detail::StripContext<typename Detail::DomainFunctionTraits<decltype(Function)>::Arguments>::Parameters>::kValue;

    /**
     * @brief The registry of a game's own value types and operations that formulas may use
     *
     * A game registers at start-up, before its content loads (`Domain/CLAUDE.md`): `RegisterType<CellMask>("mask")`, then `AddOperation<&Popcount>("popcount")`. Both are
     * append-only: an id never moves or goes away, so a program in the pool keeps meaning what it meant. `Type(id)` and `Operation(id)` take no lock and return an address that
     * never changes; registering and finding by name take one. A registration that is refused returns the reason and changes nothing.
     */
    class ProgramDomain {
    public:
        static constexpr size_t kMaxTypes = 256;
        static constexpr size_t kMaxOperations = 4096;

        /// Registers @p T under @p name, the word a formula and its messages use. Refused: a name that is no identifier, is taken or names an engine type, a type that is registered
        /// already, one over `kProgramRegisterBytes`, and `Bitwise` equality for a type without unique object representations
        template <DomainValueClass T>
        static std::expected<DomainTypeId, String> RegisterType(StringView name, DomainTypeOptions options = {}) {
            const bool unique = std::has_unique_object_representations_v<T>;
            const DomainEquality equality = options.equality.value_or(unique ? DomainEquality::Bitwise : DomainEquality::None);
            return RegisterTypeImpl(name, DescribeType<T>(), sizeof(T), equality, unique);
        }

        /**
         * @brief Registers @p Function as an operation a formula calls by @p name
         *
         * Its parameters (at most three) and its result are program types, reflected enums or registered types, and it may take `const Context&` first. Refused: a name that is no
         * identifier, an engine function (`FindExprFunction`, `at`, `ByLevel`), more than three parameters, a parameter or result type that is not registered, and a second function
         * of the same name with the same parameters (overloads differ in their parameters, not in the result).
         */
        template <auto Function>
        requires DomainFunction<Function> static std::expected<DomainOperationId, String> AddOperation(StringView name) {
            using Traits = Detail::DomainFunctionTraits<decltype(Function)>;
            using Split = Detail::StripContext<typename Traits::Arguments>;
            constexpr bool kTakesContext = Split::kTakesContext;
            return AddOperationOf<Function, kTakesContext, typename Traits::Result>(name, typename Split::Parameters{});
        }

        [[nodiscard]] static Optional<DomainTypeId> FindType(StringView name);

        /// The registered type whose descriptor is the same type as @p descriptor (`SameType`); how the checker knows a context field's type
        [[nodiscard]] static Optional<DomainTypeId> FindTypeByDescriptor(const TypeDesc& descriptor);

        [[nodiscard]] static const DomainType& Type(DomainTypeId id);

        [[nodiscard]] static size_t TypeCount();

        /// Every overload registered under @p name, in registration order
        [[nodiscard]] static Vector<DomainOperationId> FindOperations(StringView name);

        [[nodiscard]] static const DomainOperation& Operation(DomainOperationId id);

        [[nodiscard]] static size_t OperationCount();

        /// A hash of every registered name, signature and id in order: equal in two processes exactly when they registered the same set in the same order (what a generated set records)
        [[nodiscard]] static uint64_t Fingerprint();

        /// The type of a C++ type as an operand: its program type, or the registered type found by descriptor (`nullopt` when the game has not registered it)
        template <typename T>
        [[nodiscard]] static Optional<DomainValueType> ValueTypeOf() {
            DomainValueType type;
            if constexpr (IsReflectedEnum<T>) {
                type.type = ProgramType::Enum;
                type.enumType = &DescribeType<T>();
            } else if constexpr (ProgramValue<T>) {
                type.type = kProgramTypeOf<T>;
            } else {
                const Optional<DomainTypeId> domain = FindTypeByDescriptor(DescribeType<T>());
                if (!domain) {
                    return std::nullopt;
                }
                type.domain = *domain;
            }
            return type;
        }

    private:
        static std::expected<DomainTypeId, String> RegisterTypeImpl(StringView name, const TypeDesc& descriptor, size_t size, DomainEquality equality, bool uniqueRepresentation);

        static std::expected<DomainOperationId, String> AddOperationImpl(StringView name, Span<const DomainValueType> parameters, const DomainValueType& result, bool takesContext,
                                                                         DomainOperationFn function);

        template <auto Function, bool TakesContext, typename Result, typename... Parameters>
        static std::expected<DomainOperationId, String> AddOperationOf(StringView name, Detail::DomainTypeList<Parameters...>) {
            if constexpr (sizeof...(Parameters) > kMaxDomainArguments) {
                return std::unexpected(Format("operation '{}' takes {} parameters, the most is {}", name, sizeof...(Parameters), kMaxDomainArguments));
            } else {
                const Optional<DomainValueType> result = ValueTypeOf<std::remove_cvref_t<Result>>();
                if (!result) {
                    return std::unexpected(Format("operation '{}': its result type is not registered", name));
                }
                Array<DomainValueType, kMaxDomainArguments> parameters{};
                size_t index = 0;
                String refusal;
                const auto take = [&](const Optional<DomainValueType>& type) {
                    if (!type) {
                        refusal = Format("operation '{}': parameter {} has a type that is not registered", name, index + 1);
                        return false;
                    }
                    parameters[index++] = *type;
                    return true;
                };
                if (!(take(ValueTypeOf<std::remove_cvref_t<Parameters>>()) && ...)) {
                    return std::unexpected(std::move(refusal));
                }
                return AddOperationImpl(name, Span<const DomainValueType>(parameters.data(), sizeof...(Parameters)), *result, TakesContext,
                                        Detail::RawOperationOf<Function, TakesContext>(Detail::DomainTypeList<Parameters...>{}));
            }
        }
    };

}  // namespace BECore
