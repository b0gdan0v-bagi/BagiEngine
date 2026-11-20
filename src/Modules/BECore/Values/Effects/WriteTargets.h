#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>

namespace BECore {

    /**
     * @brief The paths of a context a script may write: what the host allows, checked when a script loads
     *
     * A path is `level`, `level.object` or `level.field`; it permits itself and everything under it, cut at a segment boundary (`cell` permits `cell.timer`, `cell.t` does not permit
     * `cell.timer`). An empty set permits nothing. Allocates; it is built once, before content loads.
     */
    class WriteTargets {
    public:
        /// Adds @p path; false (and nothing added) for a text that is no dotted path of identifiers (an empty segment, a `[`, a space)
        bool Allow(StringView path);

        [[nodiscard]] bool Permits(StringView path) const;

        /// The allowed paths in the order they were added, for a message
        [[nodiscard]] Span<const String> Paths() const {
            return Span<const String>(_paths.data(), _paths.size());
        }

        [[nodiscard]] bool Empty() const {
            return _paths.empty();
        }

    private:
        Vector<String> _paths;
    };

}  // namespace BECore
