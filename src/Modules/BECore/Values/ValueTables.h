#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief The named tables of numbers a game supplies to the formulas it loads (`ByLevel('FireballDamage', ability.level)`)
     *
     * The compiler copies the entries of a table into the program while the formula loads and keeps no pointer to the provider, so a provider needs to outlive the load only and a table that
     * changes later does not change a program already loaded (reload the content). Nothing here runs per frame; `Find` may be called from the thread that loads.
     */
    class IValueTables {
    public:
        virtual ~IValueTables() = default;

        /// The entries of the table @p name, valid until the next call on the provider; none when there is no such table. The first entry is index 0
        [[nodiscard]] virtual Optional<Span<const double>> Find(StringView name) const = 0;

        /// Adds the names of the tables to @p out, for the error of an unknown name (`did you mean`); a provider that cannot list them leaves it empty
        virtual void Names(Vector<String>& out) const {
            static_cast<void>(out);
        }
    };

    /// Tables kept in memory: for tests and for a game whose numbers are code. Fill it before the load: it is read-only while a document loads, so two threads may read one
    class ValueTableSet final : public IValueTables {
    public:
        /// Adds the table @p name, or replaces the one of that name
        void Add(StringView name, Vector<double> entries);

        [[nodiscard]] Optional<Span<const double>> Find(StringView name) const override;
        void Names(Vector<String>& out) const override;

    private:
        /// Sorted, so the names an error lists come in the same order every time
        FlatMap<String, Vector<double>> _tables;
    };

}  // namespace BECore
