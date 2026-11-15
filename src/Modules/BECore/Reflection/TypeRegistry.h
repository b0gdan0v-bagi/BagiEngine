#pragma once

#include <BECore/RefCounted/New.h>
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

    /// A method seen from the type it is asked for: its owner (this type or an ancestor) and where the owner's subobject sits in the object
    struct MethodRef {
        const MethodDesc* method = nullptr;
        const TypeInfo* owner = nullptr;
        /// From the address of the asked-for type's object to the owner's subobject, the ancestors' subobject offsets added
        size_t offset = 0;

        explicit operator bool() const {
            return method != nullptr;
        }
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

        /// How many types `Register` refused for a hash that another type already has: not 0 is a build-time bug (two names that hash alike, or two types of one name)
        [[nodiscard]] size_t RefusedCount() const {
            return _refused;
        }

        /// The reflected parent, or null when the type has none or its library is not linked
        [[nodiscard]] const TypeInfo* Parent(const TypeInfo& info) const;

        /// The parent, its parent, ... up to the root: the type itself is not in the list
        [[nodiscard]] Vector<const TypeInfo*> Bases(const TypeInfo& info) const;

        /// The types whose parent is @p info (or, with @p recursive, whose chain contains it), sorted by name
        [[nodiscard]] const Vector<const TypeInfo*>& Derived(const TypeInfo& info, bool recursive) const;

        /// True when @p base is @p derived or one of its ancestors
        [[nodiscard]] bool IsDerivedFrom(const TypeInfo& derived, const TypeInfo& base) const;

        /// Where the subobject of the reflected ancestor @p base sits in an object of @p derived; none when @p base is not @p derived or one of its ancestors
        [[nodiscard]] Optional<size_t> OffsetOfBase(const TypeInfo& derived, const TypeInfo& base) const;

        /// The types that are the base of an `AbstractFactory` (what `TypeInfo::factoryBase` names), sorted by name
        [[nodiscard]] const Vector<const TypeInfo*>& FactoryBases() const;

        /**
         * @brief Makes an object of the type, owned by the handle; empty for an unknown name, an abstract type or a type with no factory
         *
         * The handle holds the address of the made type (the derived one). An unknown name allocates nothing.
         */
        [[nodiscard]] OwnedObject Create(StringView name) const;
        [[nodiscard]] OwnedObject Create(const TypeInfo& info) const;

        /**
         * @brief Makes an object of the type named @p name as an `IntrusivePtr<Base>`; empty when the type is unknown, cannot be made or does not derive from `Base`
         *
         * The check is the registry's own chain (`IsDerivedFrom`), and the pointer is moved to the `Base` subobject by the offsets of
         * the chain, so it is right when `Base` is not the first thing in the object. `Base` itself must be registered.
         */
        template <typename Base>
        [[nodiscard]] typename GetIntrusivePtrType<Base>::type CreateAs(StringView name) const {
            const TypeInfo* info = Find(name);
            const TypeInfo* base = Find(Base::GetStaticTypeMeta().typeHash);
            if (info == nullptr || base == nullptr) {
                return {};
            }
            const Optional<size_t> offset = OffsetOfBase(*info, *base);
            if (!offset.has_value()) {
                return {};
            }
            OwnedObject owned = Create(*info);
            if (!owned) {
                return {};
            }
            // The made object lives at the derived address and its `Base` part at the chain's offset: char arithmetic on the address of a live object
            Base* typed = reinterpret_cast<Base*>(static_cast<char*>(owned.Get()) + *offset);
            typename GetIntrusivePtrType<Base>::type result(typed);
            owned.Reset();
            return result;
        }

        /**
         * @brief The method of this name in the type or its nearest ancestor that has one; empty when there is none
         *
         * The type's own methods come first, so a name declared again in a derived type hides the parent's. Allocates nothing, never interns.
         */
        [[nodiscard]] MethodRef FindMethod(const TypeInfo& info, StringView name) const;

        /// The fields of the type and of its ancestors, the root's first (the order `Serialize` writes them), with their offsets in @p info's object
        [[nodiscard]] Vector<FieldRef> AllFields(const TypeInfo& info) const;

    private:
        using DerivedMap = HashMap<uint64_t, Vector<const TypeInfo*>>;

        void EnsureIndex() const;
        const Vector<const TypeInfo*>& DerivedFrom(const DerivedMap& map, const TypeInfo& info) const;

        Vector<const TypeInfo*> _infos;
        HashMap<uint64_t, const TypeInfo*> _byHash;
        size_t _refused = 0;

        // Built at the first query after the last registration; the lock is taken only to build
        mutable Mutex _indexMutex{BE_LOCK_SITE("Type registry index")};
        mutable std::atomic<size_t> _indexedCount{0};
        mutable DerivedMap _direct;
        mutable DerivedMap _all;
        mutable Vector<const TypeInfo*> _factoryBases;
        Vector<const TypeInfo*> _none;
    };

}  // namespace BECore
