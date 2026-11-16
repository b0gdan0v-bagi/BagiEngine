#pragma once

#include <BECore/Reflection/FieldAttributes.h>
#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Reflection/PropertyPath.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Types/Types.h>
#include <cmath>
#include <cstddef>
#include <expected>
#include <type_traits>

namespace BECore {

    /**
     * @brief The fixed part of a property path, resolved once
     *
     * The fixed part is the run of segments that name fields of the root type (its parents' too) and of objects embedded in it by
     * value: each of them sits at a constant distance from the root object, so it is found once and added up. It ends at the first
     * segment that needs the live object (an element of a list or a map, the value of an optional or a variant, a member of a pair
     * or of a pointee): that segment and the ones after it are the `rest`.
     */
    struct CompiledPath {
        /// The type the path was compiled against; an object it is read from is an object of this type (or of a type derived from it)
        const TypeInfo* root = nullptr;
        /// From the address of a root object to the value where the fixed part ends
        size_t offset = 0;
        /// The type of the value where the fixed part ends: the field's own type, a list or a struct as well as a number
        const TypeDesc* type = nullptr;
        size_t fixedSegments = 0;
        /// The path text after the fixed part (`[2].c`, `.value`); empty when the whole path is fixed
        String rest;
        /// `readonly` or `hidden` on any field the fixed part passed: a write is refused
        bool readOnly = false;
        bool hidden = false;
        /// What the field where the fixed part ends says (`range` for a numeric value)
        FieldAttributes leaf;

        // What the erased read continues from: the innermost embedded object the fixed part reached, and the segments that start at its field
        const TypeInfo* owner = nullptr;
        size_t ownerOffset = 0;
        PropertyPath tail;

        [[nodiscard]] bool IsFixed() const {
            return rest.empty();
        }
    };

    /**
     * @brief Resolves the fixed part of @p path in the type @p root, once
     *
     * A field is named as `GetProperty` names it (the serialized name, a parent's field like an own one). The path may end at any
     * kind of value, a list or a struct too: that is what `CompileProperty<Vector<int>>` reads. Errors: `invalid_path` for a bad text,
     * `not_found` for a field that the type does not have (or a segment that names an element of a struct or a member of a value).
     */
    [[nodiscard]] std::expected<CompiledPath, PropertyError> CompilePath(const TypeInfo& root, StringView path, const TypeRegistry& registry = TypeRegistry::GetInstance());

    /**
     * @brief Reads the value of a compiled path from @p object as `GetProperty` would
     *
     * @p object is an object of the compiled type or of a type derived from it. A number or a bool at the end of a fully fixed path
     * is read by its kind from the offset; every other value, and a path with a rest, continues with `GetProperty` on the embedded
     * object where the fixed part ended. `type_mismatch` when @p object is not of the compiled type.
     */
    [[nodiscard]] std::expected<PropertyValue, PropertyError> ReadCompiled(const CompiledPath& path, TypedRef object, const TypeRegistry& registry = TypeRegistry::GetInstance());

    namespace Detail {

        template <typename T>
        concept NumericValue = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;

        template <typename T>
        concept NumericList = requires(const T& list) {
            requires NumericValue<std::remove_cvref_t<decltype(*list.begin())>>;
            list.end();
        };

        /// The `range` of a field applies to a number and to the numbers of a list field; false when @p value is outside it
        template <typename T>
        bool WithinRange(const FieldAttributes& attributes, const T& value) {
            if (!attributes.hasRange) {
                return true;
            }
            if constexpr (NumericValue<T>) {
                const auto wide = static_cast<double>(value);
                return wide >= attributes.rangeMin && wide <= attributes.rangeMax;
            } else if constexpr (NumericList<T>) {
                for (const auto& element : value) {
                    if (!WithinRange(attributes, element)) {
                        return false;
                    }
                }
            }
            return true;
        }

    }  // namespace Detail

    /**
     * @brief A typed handle on one fully fixed property: reading it is an offset added to an address
     *
     * `Get` and `Mutable` do no lookup, no allocation and no check, so a reader that knows the type and the path reads as fast as a
     * member access. `Set` is the checked write: it still refuses a `readonly` or `hidden` path and a value outside `range`, because
     * a handle can be kept by code that has no idea what the field allows.
     */
    template <typename T>
    class CompiledProperty {
    public:
        CompiledProperty() = default;

        /// Made by `CompileProperty` from a compiled path; nothing else has a reason to
        CompiledProperty(size_t offset, bool readOnly, bool hidden, const FieldAttributes& leaf) : _offset(offset), _readOnly(readOnly), _hidden(hidden), _leaf(leaf) {}

        /// @p object is the address of an object of the compiled root type (a base subobject of a derived object works)
        [[nodiscard]] const T& Get(const void* object) const {
            return *reinterpret_cast<const T*>(static_cast<const char*>(object) + _offset);
        }

        /// The unchecked write access: it does not look at `readonly`, `hidden` or `range`
        [[nodiscard]] T& Mutable(void* object) const {
            return *reinterpret_cast<T*>(static_cast<char*>(object) + _offset);
        }

        /// Writes @p value; false (and nothing is changed) for a `readonly` or `hidden` path or a value outside `range`
        bool Set(void* object, const T& value) const {
            if (_readOnly || _hidden || !Detail::WithinRange(_leaf, value)) {
                return false;
            }
            Mutable(object) = value;
            return true;
        }

        [[nodiscard]] size_t Offset() const {
            return _offset;
        }

    private:
        size_t _offset = 0;
        bool _readOnly = false;
        bool _hidden = false;
        FieldAttributes _leaf;
    };

    /**
     * @brief The handle of a fully fixed path whose value is a `T`
     *
     * `not_fixed` when the path has a rest (the value is behind a list, a map, an optional, a pair or a pointer: use `GetProperty`
     * or `ReadCompiled`), `type_mismatch` when the field is not a `T` (compared with `SameType`).
     */
    template <typename T>
    requires Describable<std::remove_cv_t<T>> [[nodiscard]] std::expected<CompiledProperty<T>, PropertyError> CompileProperty(const TypeInfo& root, StringView path,
                                                                                                                              const TypeRegistry& registry = TypeRegistry::GetInstance()) {
        auto compiled = CompilePath(root, path, registry);
        if (!compiled.has_value()) {
            return std::unexpected(std::move(compiled.error()));
        }
        if (!compiled->IsFixed()) {
            return std::unexpected(PropertyError{PropertyErrorCode::not_fixed, Format("path '{}': the part '{}' needs the live object, so the path is not fixed", path, compiled->rest)});
        }
        if (!SameType(DescribeType<T>(), *compiled->type)) {
            return std::unexpected(PropertyError{PropertyErrorCode::type_mismatch, Format("path '{}': the field is not of the type asked for", path)});
        }
        return CompiledProperty<T>(compiled->offset, compiled->readOnly, compiled->hidden, compiled->leaf);
    }

}  // namespace BECore
