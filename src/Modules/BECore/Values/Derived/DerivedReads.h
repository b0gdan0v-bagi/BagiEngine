#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// How many plain counters a derived value may watch beside its levels: a hit compares each one, so the number stays small
    inline constexpr size_t kMaxVersionSources = 4;

    /**
     * @brief What a `Derived` value reads, declared once: levels of a `Context` and plain version counters that belong to no level
     *
     * A level is declared by the `Value` that reads it (`Add`), by index or by name against a `ContextDecl` (`AddLevel`, where an
     * unknown name is a `LinkError` that lists the declared levels, so a typo shows at load). A source is the address of a counter
     * its owner bumps (`UIVars`' table has one): it must outlive the value and only grow. Nothing declared means the value is
     * built once.
     */
    class DerivedReads {
    public:
        /// The level @p value reads, nothing for a `Const`
        template <ValueType T>
        DerivedReads& Add(const Value<T>& value) {
            _mask |= value.Reads();
            return *this;
        }

        /// Every level of @p mask, for a mask a `ValueDependencies` collected
        DerivedReads& AddLevels(LevelMask mask) {
            _mask |= mask;
            return *this;
        }

        DerivedReads& AddLevel(LevelIndex level) {
            ASSERT(level < kMaxContextLevels, "DerivedReads::AddLevel: no such level");
            if (level < kMaxContextLevels) {
                _mask |= LevelBit(level);
            }
            return *this;
        }

        /// The level named @p name in @p declaration; `unknown_level` when it has none, the message names the level, the declared ones and the nearest
        [[nodiscard]] std::expected<void, LinkError> AddLevel(const ContextDecl& declaration, StringView name);

        /**
         * @brief Watches @p counter: the value is rebuilt when it changes
         *
         * False and an `ASSERT` past `kMaxVersionSources`; the source is never dropped silently, so a screen cannot go stale without a message.
         */
        [[nodiscard]] bool AddSource(const uint64_t& counter);

        [[nodiscard]] LevelMask Mask() const {
            return _mask;
        }

        [[nodiscard]] Span<const uint64_t* const> Sources() const {
            return Span<const uint64_t* const>(_sources.data(), _sources.size());
        }

        /// No level and no source: the value is built once
        [[nodiscard]] bool IsEmpty() const {
            return _mask == 0 && _sources.empty();
        }

    private:
        LevelMask _mask = 0;
        FixedVector<const uint64_t*, kMaxVersionSources> _sources;
    };

}  // namespace BECore
