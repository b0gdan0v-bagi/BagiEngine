#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>

namespace BECore {

    /// Levels of a context: a value names a level by its position, so the position is a byte
    using LevelIndex = uint8_t;

    inline constexpr size_t kMaxContextLevels = 8;

    /// A set of levels, bit `i` for level `i`: what a set of values reads from, as one byte
    using LevelMask = uint8_t;
    static_assert(kMaxContextLevels <= std::numeric_limits<LevelMask>::digits, "a LevelMask holds a bit per level");

    [[nodiscard]] constexpr LevelMask LevelBit(LevelIndex level) {
        return static_cast<LevelMask>(LevelMask{1} << level);
    }

    CORE_ENUM(LinkErrorCode, uint8_t, invalid_path, unknown_type, duplicate_level, too_many_levels, unknown_level, unknown_field, not_fixed, type_mismatch, too_far, not_a_list, bad_element)

    /// A refused declaration or link: the code and a message that names the path, the field and what is wrong
    struct LinkError {
        LinkErrorCode code = LinkErrorCode::invalid_path;
        String message;
    };

    /// One level of a context: its name in a path and the reflected type of the object a game puts there
    struct ContextLevel {
        String name;
        const TypeInfo* type = nullptr;
    };

    /**
     * @brief The levels a value may read from, by name and reflected type
     *
     * A level is an object of a game (the caster, the cast, a view of the table): the declaration says what type it is so that a
     * path into it is checked and turned into an offset once, when content is loaded. The declaration is data, the objects are in a
     * `Context`.
     */
    class ContextDecl {
    public:
        /// The level of this name and reflected type (`TypeRegistry`: the simple class name); `unknown_type`, `duplicate_level` or `too_many_levels` when it is refused
        [[nodiscard]] std::expected<LevelIndex, LinkError> Add(StringView name, StringView typeName, const TypeRegistry& registry = TypeRegistry::GetInstance());

        /// The level of this name and the reflected type `T` (a `BE_CLASS` that is registered)
        template <typename T>
        [[nodiscard]] std::expected<LevelIndex, LinkError> Add(StringView name, const TypeRegistry& registry = TypeRegistry::GetInstance()) {
            return Add(name, T::GetStaticTypeMeta().typeName, registry);
        }

        [[nodiscard]] Optional<LevelIndex> IndexOf(StringView name) const;

        [[nodiscard]] Span<const ContextLevel> Levels() const {
            return Span<const ContextLevel>(_levels.data(), _levels.size());
        }

        [[nodiscard]] size_t Size() const {
            return _levels.size();
        }

        /// The editor has nothing to edit in a declaration: it is the document's `<context>`, not a property
        template <typename Visitor>
        bool VisitProperties(Visitor& /*visitor*/) {
            return false;
        }

    private:
        FixedVector<ContextLevel, kMaxContextLevels> _levels;
    };

    /**
     * @brief The objects a set of values reads from, one pointer per level
     *
     * Nothing is copied: the context holds the address of each object, so the game writes its own plain fields and a value reads
     * them where they are. The objects and the declaration outlive the context. A level holds the address of the object **as the
     * declared type**, so an object of a derived type is stored at its base subobject.
     *
     * A level may also name a version counter the owner of the object bumps when the object changes; the
     * counter outlives the context and only grows. `VersionKey` folds the counters of some levels into one number that tells whether
     * anything those levels hold has changed, so a screen rebuilds only when its key moves.
     */
    class Context {
    public:
        Context() = default;

        /// With a declaration `Set` checks the level and the type of the object
        explicit Context(const ContextDecl& declaration) : _declaration(&declaration) {}

        /// Puts @p object at @p level; @p version is the counter its owner bumps on a change, null for an object nobody versions (version 0)
        template <typename T>
        requires requires {
            T::GetStaticTypeMeta();
        }
        void Set(LevelIndex level, const T& object, const uint64_t* version = nullptr) {
            SetObject(level, std::addressof(object), T::GetStaticTypeMeta(), version, false);
        }

        /**
         * @brief Puts @p object at @p level like `Set` and marks the level writable, so a script may store into it
         *
         * @p version is the counter a store bumps when it changes a byte (`BumpVersion`), null for an object nobody versions. A later `Set` of the level clears the mark.
         */
        template <typename T>
        requires requires {
            T::GetStaticTypeMeta();
        }
        void SetMutable(LevelIndex level, T& object, uint64_t* version = nullptr) {
            SetObject(level, std::addressof(object), T::GetStaticTypeMeta(), version, true);
        }

        /// The object of the level as the declared type, null while none is set
        [[nodiscard]] const void* Object(LevelIndex level) const {
            ASSERT(level < kMaxContextLevels);
            return _objects[level];
        }

        [[nodiscard]] bool IsMutable(LevelIndex level) const {
            return level < kMaxContextLevels && (_mutable & LevelBit(level)) != 0;
        }

        /// The object of a level that `SetMutable` filled, null for any other level
        [[nodiscard]] void* MutableObject(LevelIndex level) const {
            // The pointer came from a non-const reference in `SetMutable`, the only place that marks a level, so writing through it is not a write to a const object
            return IsMutable(level) ? const_cast<void*>(_objects[level]) : nullptr;
        }

        /// Counts a change of the level's object for `VersionKey`; nothing for a level with no counter
        void BumpVersion(LevelIndex level) const {
            ASSERT(level < kMaxContextLevels);
            if (IsMutable(level) && _versions[level] != nullptr) {
                // Same origin as `MutableObject`: a mutable level's counter was given as `uint64_t*`
                ++*const_cast<uint64_t*>(_versions[level]);
            }
        }

        /// The counter of the level now, 0 for a level that has none
        [[nodiscard]] uint64_t Version(LevelIndex level) const {
            ASSERT(level < kMaxContextLevels);
            return _versions[level] != nullptr ? *_versions[level] : 0;
        }

        /**
         * @brief One number for the levels of @p levels: it changes when a counter of one of them moves or one of them is given another object or counter
         *
         * Per level the fold holds the counter's value, the count of `Set` calls of the level, the address of the object and the address
         * of the counter. A context rebuilt each frame over the same objects and counters therefore gives the same key (its `Set` counts
         * restart at 1 in each), and another object or another counter object at a level gives another key even when the numbers agree.
         * A `Set` of a level that is not in the mask does not move the key. The fold is a hash, so the number itself means nothing and
         * two keys are only compared for equality. A level with no object or no counter adds a constant for it.
         *
         * What it cannot see: an object freed and another built at the same address, with a counter at the same address and value. The
         * owner bumps the counter when it replaces an object.
         */
        [[nodiscard]] uint64_t VersionKey(LevelMask levels) const;

        /// True when every level of @p levels has an object and a counter (`Set` with a version): the key then tells whether a value that reads them changed. The empty mask is true
        [[nodiscard]] bool IsVersioned(LevelMask levels) const;

    private:
        void SetObject(LevelIndex level, const void* object, const ClassMeta& type, const uint64_t* version, bool writable);
        void Store(LevelIndex level, const void* object, const uint64_t* version, bool writable);

        Array<const void*, kMaxContextLevels> _objects{};
        Array<const uint64_t*, kMaxContextLevels> _versions{};
        Array<uint32_t, kMaxContextLevels> _sets{};
        LevelMask _mutable = 0;
        const ContextDecl* _declaration = nullptr;
    };

    /// Where a value sits: the level and the distance from that level's object to the field
    struct SlotRef {
        LevelIndex level = 0;
        uint32_t offset = 0;
    };

    /// A field a path names: where it is and its own type
    struct LinkedField {
        SlotRef slot;
        const TypeDesc* type = nullptr;
    };

    /**
     * @brief Turns `level.field.path` into the slot of a field and its type, the same checks as `LinkSlot` without a type to match
     *
     * What a script's write target and a formula's operand both need: the type tells what the field may hold. The refusals and their messages are those of `LinkSlot`
     * (`unknown_level`, `unknown_field`, `not_fixed`, `too_far`, ...).
     */
    [[nodiscard]] std::expected<LinkedField, LinkError> LinkField(const ContextDecl& declaration, StringView path, const TypeRegistry& registry = TypeRegistry::GetInstance());

    /**
     * @brief Turns `level.field.path` into a slot, checking it against the declaration and the type asked for
     *
     * The first segment names a level; the rest is a property path of the level's type that must be fully fixed (no list element,
     * map value, optional or pointee) and its field must be of the type @p expected (`SameType`: there is no conversion).
     * Allocates; it runs when content is loaded, never in a frame.
     */
    [[nodiscard]] std::expected<SlotRef, LinkError> LinkSlot(const ContextDecl& declaration, StringView path, const TypeDesc& expected, const TypeRegistry& registry = TypeRegistry::GetInstance());

    /// Where a list sits: the level, the distance from that level's object to the list field and the field's type (static storage), from which `MakeListDescriptor` takes the layout
    struct ListLink {
        LevelIndex level = 0;
        uint32_t offset = 0;
        const TypeDesc* list = nullptr;
    };

    /// Where a field sits in the element of a list: the distance from the element and the field's own type, a value or a list (the caller looks at `type->kind`)
    struct ElementLink {
        uint32_t offset = 0;
        const TypeDesc* type = nullptr;
    };

    /**
     * @brief Turns `level.field.path` into the list it names, as `LinkSlot` does for a value
     *
     * The path must end at a list field and be fully fixed; the list's element must be one a formula can read (`DescribeListElement`). `not_a_list` says what the field is instead, `bad_element`
     * what a list may hold. Allocates; it runs when content is loaded.
     */
    [[nodiscard]] std::expected<ListLink, LinkError> LinkList(const ContextDecl& declaration, StringView path, const TypeRegistry& registry = TypeRegistry::GetInstance());

    /**
     * @brief The field `a.b.c` of an element of type @p element, fully fixed (embedded objects are crossed, an element of a list inside is not)
     *
     * The messages name @p path as given (the fields after the element's name) and the element's type. No type is asked for: the caller reads `type->kind` and compares as it needs.
     */
    [[nodiscard]] std::expected<ElementLink, LinkError> LinkElementField(const TypeInfo& element, StringView path, const TypeRegistry& registry = TypeRegistry::GetInstance());

    /**
     * @brief The type of the elements of the list type @p list: null for a scalar, an enum, a math type or a `PoolString`, the registered class for an object
     *
     * Refuses (`bad_element`) what a formula cannot read: a list of pointers, maps, optionals, variants, pairs, lists or strings that own their text; the message says what a list may hold.
     */
    [[nodiscard]] std::expected<const TypeInfo*, LinkError> DescribeListElement(const TypeDesc& list, const TypeRegistry& registry = TypeRegistry::GetInstance());

}  // namespace BECore

// The accessor is part of the type: a generated .gen.hpp that names a ContextDecl field sees it by including this header
#include <BECore/Values/ContextDeclAccessor.h>
