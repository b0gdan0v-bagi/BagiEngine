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

    CORE_ENUM(LinkErrorCode, uint8_t, invalid_path, unknown_type, duplicate_level, too_many_levels, unknown_level, unknown_field, not_fixed, type_mismatch, too_far)

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
            SetObject(level, std::addressof(object), T::GetStaticTypeMeta(), version);
        }

        /// The object of the level as the declared type, null while none is set
        [[nodiscard]] const void* Object(LevelIndex level) const {
            ASSERT(level < kMaxContextLevels);
            return _objects[level];
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
        void SetObject(LevelIndex level, const void* object, const ClassMeta& type, const uint64_t* version);
        void Store(LevelIndex level, const void* object, const uint64_t* version);

        Array<const void*, kMaxContextLevels> _objects{};
        Array<const uint64_t*, kMaxContextLevels> _versions{};
        Array<uint32_t, kMaxContextLevels> _sets{};
        const ContextDecl* _declaration = nullptr;
    };

    /// Where a value sits: the level and the distance from that level's object to the field
    struct SlotRef {
        LevelIndex level = 0;
        uint32_t offset = 0;
    };

    /**
     * @brief Turns `level.field.path` into a slot, checking it against the declaration and the type asked for
     *
     * The first segment names a level; the rest is a property path of the level's type that must be fully fixed (no list element,
     * map value, optional or pointee) and its field must be of the type @p expected (`SameType`: there is no conversion).
     * Allocates; it runs when content is loaded, never in a frame.
     */
    [[nodiscard]] std::expected<SlotRef, LinkError> LinkSlot(const ContextDecl& declaration, StringView path, const TypeDesc& expected, const TypeRegistry& registry = TypeRegistry::GetInstance());

}  // namespace BECore

// The accessor is part of the type: a generated .gen.hpp that names a ContextDecl field sees it by including this header
#include <BECore/Values/ContextDeclAccessor.h>
