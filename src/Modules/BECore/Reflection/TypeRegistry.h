#pragma once

#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Types/Types.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// A field of a type seen from the type it is asked for: its owner (this type or an ancestor) and where it sits in the object
    struct FieldRef {
        const FieldDesc* field = nullptr;
        const TypeInfo* owner = nullptr;
        /// From the address of the asked-for type's object to the field, the ancestors' subobject offsets included
        size_t offset = 0;
    };

    /**
     * @brief Every reflected type of the program, found by name or hash
     *
     * A Meyers singleton like `AbstractFactory`. **Registration happens in static initializers only** (the block the generator writes
     * into every `.gen.hpp`), so a library must be linked whole, as for the factory: a type whose library is dropped is not
     * registered. Every read after start-up is lock-free; `Find` during static initialization is unsupported (the order across
     * translation units is not defined), and so is a `Register` after the first read.
     *
     * A lookup never interns: a client's text must not grow the `PoolString` table (`Control/CLAUDE.md`), so `Find(StringView)`
     * hashes the text and allocates nothing. The name of a type is its simple class name, so two types of one name in different
     * namespaces clash: the second is refused and logged.
     *
     * The constructor is public so that a test can use a registry of its own; the engine uses `GetInstance()`.
     */
    class TypeRegistry {
    public:
        TypeRegistry() = default;
        TypeRegistry(const TypeRegistry&) = delete;
        TypeRegistry& operator=(const TypeRegistry&) = delete;

        static TypeRegistry& GetInstance();

        /**
         * @brief Adds a type; false when it was not added
         *
         * The same `TypeInfo` again is ignored. A different one with the same hash (two names that hash alike, or two types of one
         * name) is logged as an error and refused: `ASSERT` compiles out in Release.
         */
        bool Register(const TypeInfo& info);

        /// The type of this class name, or null; allocates nothing
        [[nodiscard]] const TypeInfo* Find(StringView name) const;
        [[nodiscard]] const TypeInfo* Find(uint64_t hash) const;

        /// Every registered type, in registration order (not stable across builds: sort for a stable listing)
        [[nodiscard]] const Vector<const TypeInfo*>& All() const {
            return _infos;
        }

        /// The reflected parent, or null when the type has none or its library is not linked
        [[nodiscard]] const TypeInfo* Parent(const TypeInfo& info) const;

        /// The parent, its parent, ... up to the root: the type itself is not in the list
        [[nodiscard]] Vector<const TypeInfo*> Bases(const TypeInfo& info) const;

        /// The types whose parent is @p info (or, with @p recursive, whose chain contains it), sorted by name
        [[nodiscard]] const Vector<const TypeInfo*>& Derived(const TypeInfo& info, bool recursive) const;

        /// True when @p base is @p derived or one of its ancestors
        [[nodiscard]] bool IsDerivedFrom(const TypeInfo& derived, const TypeInfo& base) const;

        /// The fields of the type and of its ancestors, the root's first (the order `Serialize` writes them), with their offsets in @p info's object
        [[nodiscard]] Vector<FieldRef> AllFields(const TypeInfo& info) const;

    private:
        using DerivedMap = HashMap<uint64_t, Vector<const TypeInfo*>>;

        void EnsureDerivedIndex() const;
        const Vector<const TypeInfo*>& DerivedFrom(const DerivedMap& map, const TypeInfo& info) const;

        Vector<const TypeInfo*> _infos;
        HashMap<uint64_t, const TypeInfo*> _byHash;

        // Built at the first query after the last registration; the lock is taken only to build
        mutable Mutex _indexMutex{BE_LOCK_SITE("Type registry index")};
        mutable std::atomic<size_t> _indexedCount{0};
        mutable DerivedMap _direct;
        mutable DerivedMap _all;
        Vector<const TypeInfo*> _none;
    };

}  // namespace BECore
