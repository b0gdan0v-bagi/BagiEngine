#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/ValueType.h>
#include <concepts>
#include <cstdint>
#include <expected>

namespace BECore {

    /// What a value is: a literal, a read of a field of a context object, or a compiled formula (`Programs/`)
    CORE_ENUM(ValueKind, uint8_t, Const, Slot, Program)

    /**
     * @brief A number, a bool, a text, an enum or a small object that data may leave as a literal or read from a context
     *
     * A field of this type loads `damage="12"` as before, and `<damage from="caster.attack"/>` reads the field `attack` of the object
     * a game puts at the level `caster` of the `Context`: the path is checked against the declared type and turned into an offset once, when
     * the content is loaded, so `Get` is a branch and an addition. It converts from `T` implicitly, so code that fills a field keeps its look.
     * The literal is a plain member beside the slot (no union), so a `Value` is always a valid literal of its type. A `Program` value holds the
     * id of a compiled formula in the `ProgramPool` (in the field a `Slot` uses for its offset) and its text (in the path field): it stays trivially
     * copyable, and `Evaluate` runs it.
     */
    template <ValueType T>
    class Value {
    public:
        using ValueMarker = void;

        constexpr Value() = default;

        // NOLINTNEXTLINE(google-explicit-constructor): a literal is the common case and reads as a plain field
        constexpr Value(const T& literal) : _constant(literal) {}

        [[nodiscard]] static constexpr Value Const(const T& literal) {
            return Value(literal);
        }

        /// A read of the slot @p slot, found from @p path (kept so a save writes it back); the caller made the slot from that path and the type `T`
        [[nodiscard]] static Value FromSlot(SlotRef slot, PoolString path) {
            Value value;
            value._kind = ValueKind::Slot;
            value._level = slot.level;
            value._offset = slot.offset;
            value._path = path;
            return value;
        }

        /// Makes this value a read of @p path (`level.field.path`) of the declared context; it stays as it was when the path is refused
        [[nodiscard]] std::expected<void, LinkError> Link(const ContextDecl& declaration, StringView path, const TypeRegistry& registry = TypeRegistry::GetInstance()) {
            const auto slot = LinkSlot(declaration, path, DescribeType<T>(), registry);
            if (!slot.has_value()) {
                return std::unexpected(slot.error());
            }
            *this = FromSlot(*slot, PoolString::Intern(path));
            return {};
        }

        /// A value of the program @p id of the `ProgramPool`, written as @p source; the caller made the program for the type `T`
        [[nodiscard]] static Value FromProgram(ProgramId id, PoolString source) {
            Value value;
            value._kind = ValueKind::Program;
            value._offset = id;
            value._path = source;
            return value;
        }

        [[nodiscard]] constexpr ValueKind Kind() const {
            return _kind;
        }

        [[nodiscard]] constexpr bool IsConst() const {
            return _kind == ValueKind::Const;
        }

        [[nodiscard]] constexpr bool IsSlot() const {
            return _kind == ValueKind::Slot;
        }

        /// The path of a `Slot`, the text of a `Program`, empty for a `Const`
        [[nodiscard]] constexpr const PoolString& Path() const {
            return _path;
        }

        /// The level a `Slot` reads from (what a set of values depends on); 0 for the other kinds
        [[nodiscard]] constexpr LevelIndex Level() const {
            return _level;
        }

        /// The distance from the level's object to the field of a `Slot`; 0 for a `Const`. Of a `Program` it is its id, see `Id()`
        [[nodiscard]] constexpr uint32_t Offset() const {
            return _offset;
        }

        /// The id of a `Program` in the `ProgramPool`; meaningless for the other kinds
        [[nodiscard]] constexpr ProgramId Id() const {
            return _offset;
        }

        /// The levels the value reads: one for a `Slot`, the program's for a `Program`, none for a `Const`; what to ask `Context::VersionKey` about
        [[nodiscard]] constexpr LevelMask Reads() const {
            switch (_kind) {
                case ValueKind::Slot:
                    return LevelBit(_level);
                case ValueKind::Program:
                    return ProgramPool::Get(_offset).Reads();
                case ValueKind::Const:
                    break;
            }
            return LevelMask{0};
        }

        /// The literal; of a value that is not a `Const` it is the default of `T`
        [[nodiscard]] constexpr const T& GetConst() const {
            return _constant;
        }

        /**
         * @brief The value now: the literal, or the field of the context object a `Slot` names
         *
         * Reads in place, so the reference is valid while the object is: a null level is a bug of the caller (`ASSERT`, compiled out in
         * Release). A game that runs this per frame copies out what it needs. A `Program` has no object to point into: use `Evaluate`.
         */
        [[nodiscard]] const T& Get(const Context& context) const {
            if (_kind == ValueKind::Slot) {
                const void* object = context.Object(_level);
                ASSERT(object != nullptr, "Value::Get: the context has no object at the level this value reads from");
                return *static_cast<const T*>(static_cast<const void*>(static_cast<const char*>(object) + _offset));
            }
            ASSERT(_kind == ValueKind::Const, "Value::Get: a program computes its value, it is not stored anywhere: use Evaluate");
            return _constant;
        }

        /**
         * @brief The value now, by value, for every kind: `Get` for a `Const` and a `Slot`, a run of the program for a `Program`
         *
         * Only a type a program can compute (`ProgramValue`) has programs; asking another `T` for a `Program` is a bug of the loader (`ASSERT`).
         * A run allocates nothing (`Programs/Interpreter.h`).
         */
        [[nodiscard]] T Evaluate(const Context& context) const {
            if (_kind == ValueKind::Program) {
                if constexpr (ProgramValue<T>) {
                    return Run<T>(_offset, context);
                } else {
                    ASSERT(false, "Value::Evaluate: no program computes this type");
                    return _constant;
                }
            }
            return Get(context);
        }

        /// Two programs are equal when they are the same program (its id) written the same way; a `Slot` by where it reads
        [[nodiscard]] constexpr bool operator==(const Value& other) const requires std::equality_comparable<T> {
            if (_kind != other._kind) {
                return false;
            }
            if (_kind == ValueKind::Program) {
                return _offset == other._offset && _path == other._path;
            }
            if (_kind == ValueKind::Slot) {
                return _level == other._level && _offset == other._offset && _path == other._path;
            }
            return _constant == other._constant;
        }

        /// The editor shows the literal of a `Const` as a compound `value`; a `Slot` is not a literal and is not edited as one
        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            if (_kind != ValueKind::Const) {
                return false;
            }
            return PropVisit::Visit(v, _constant, "value");
        }

    private:
        T _constant{};
        PoolString _path;
        uint32_t _offset = 0;
        LevelIndex _level = 0;
        ValueKind _kind = ValueKind::Const;
    };

    /// The levels a set of values reads, for `Context::VersionKey`: add each value of a view, then ask for the mask
    class ValueDependencies {
    public:
        template <ValueType T>
        ValueDependencies& Add(const Value<T>& value) {
            _mask |= value.Reads();
            return *this;
        }

        [[nodiscard]] constexpr LevelMask Mask() const {
            return _mask;
        }

    private:
        LevelMask _mask = 0;
    };

}  // namespace BECore

// The accessor is part of the type: a generated .gen.hpp that names a Value field sees it by including this header
#include <BECore/Values/ValueAccessor.h>
