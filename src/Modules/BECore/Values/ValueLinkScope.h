#pragma once

#include <BECore/Values/Context.h>

namespace BECore {

    /**
     * @brief The context declaration the values read while a document loads: the caller of a load opens one, the accessor of `Value<T>`
     *        links the paths it reads against it
     *
     * A stack per thread, so two threads loading at once each see their own declaration and never the other's. A `from` read with no scope
     * open is a load error: the document has no context to name a level of.
     */
    class ValueLinkScope {
    public:
        explicit ValueLinkScope(const ContextDecl& declaration);
        ~ValueLinkScope();

        ValueLinkScope(const ValueLinkScope&) = delete;
        ValueLinkScope& operator=(const ValueLinkScope&) = delete;
        ValueLinkScope(ValueLinkScope&&) = delete;
        ValueLinkScope& operator=(ValueLinkScope&&) = delete;

        /// The declaration of the innermost scope open on this thread, null when there is none
        [[nodiscard]] static const ContextDecl* Current();

    private:
        const ContextDecl* _declaration;
        const ValueLinkScope* _previous;
    };

}  // namespace BECore
