#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Utils/Singleton.h>
#include <BECore/Values/Context.h>

namespace BECore {

    /// The provider the control API asks when a request names no context
    inline constexpr StringView kDefaultContextName = "default";

    /// What a context is, in the control API and in the editor's inspector: the declaration the objects were made for and the objects
    struct ExplainContext {
        /// Outlives the call that uses it: a game's own declaration
        const ContextDecl* declaration = nullptr;
        Context context;
    };

    /// Gives the context as it is now, empty when the game has none at the moment
    using ExplainContextProvider = MoveOnlyFunction<Optional<ExplainContext>()>;

    /**
     * @brief The contexts a client or the inspector may explain a `Value<T>` against, by name
     *
     * A scene has no context of its own, so a game fills this: `Register("default", ...)` for the control API, a name of its own for each context a client may name. Main thread only, like
     * `ControlRoots`. Names are held as `String` and looked up as text, never interned: a client's text must not grow the `PoolString` pool.
     */
    class ValueContextRegistry : public Singleton<ValueContextRegistry> {
    public:
        /// Registers @p provider under @p name; a provider of the same name is replaced
        void Register(StringView name, ExplainContextProvider provider);

        /// The provider @p name, null when there is none
        [[nodiscard]] const ExplainContextProvider* Find(StringView name) const;

        /// The names, sorted
        [[nodiscard]] Vector<String> Names() const;

        /// Forgets every provider (a test, a game that is torn down)
        void Clear();

    private:
        Map<String, ExplainContextProvider> _providers;
    };

}  // namespace BECore
