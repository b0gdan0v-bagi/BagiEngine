#pragma once

#include <BECore/Reflection/TypedRef.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>

namespace BECore {

    /**
     * @brief A named entry point of the control API into the object graph: what a client may reach by `root`
     *
     * @c resolve runs on every request, on the main thread, and gives the object as it is now (an empty reference when there is
     * none). The reference is not kept past the handler. @c allowInterning lets a write through the root put a client's text into
     * the `PoolString` pool, which never shrinks: off by default, so a client cannot grow the pool.
     */
    struct ControlRoot {
        MoveOnlyFunction<TypedRef()> resolve;
        bool allowInterning = false;
    };

    /**
     * @brief The roots of a `ControlService`: the only objects the by-name methods (`get_property`, `call_method`) can reach
     *
     * A service, a widget or the renderer is not a root unless a module registers it. Names are held as `String` and looked up
     * as `StringView`, never interned: a client's text must not grow the pool. Main thread only, like `RegisterHandler`.
     */
    class ControlRoots {
    public:
        /// Registers @p root under @p name; a root of the same name is replaced
        void Register(StringView name, ControlRoot root);

        /// The root @p name, or null when there is none. Never interns @p name
        [[nodiscard]] const ControlRoot* Find(StringView name) const;

        /// The names, sorted
        [[nodiscard]] Vector<String> Names() const;

        /// The object of the root @p name now; empty when the root is unknown or has no object
        [[nodiscard]] TypedRef Resolve(StringView name) const;

    private:
        Map<String, ControlRoot> _roots;
    };

}  // namespace BECore
