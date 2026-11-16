#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/ValueTables.h>
#include <expected>

namespace BECore {

    /**
     * @brief The context declaration the values read while a document loads: the loader of a document opens one, the accessor of `Value<T>`
     *        links the paths it reads against it
     *
     * A stack per thread, so two threads loading at once each see their own declaration and never the other's. A scope gets its declaration
     * from code (the constructor that takes one) or from the document itself (`<context>`, `Declare`, which keeps a copy: the object that
     * was read may move). A `from` read where no scope has a declaration is a load error: the document has no context to name a level of.
     * A scope may also carry the named tables of the game (`UseTables`) for `ByLevel`; like the declaration, they are found in the innermost scope that has them.
     */
    class ValueLinkScope {
    public:
        /// A scope whose declaration is @p declaration, which outlives it; it takes no `<context>` of its own
        explicit ValueLinkScope(const ContextDecl& declaration);

        /// An empty scope for the document @p documentName (kept for messages); the document declares its own context, or the declaration of an outer scope stays in sight
        explicit ValueLinkScope(StringView documentName);
        ~ValueLinkScope();

        ValueLinkScope(const ValueLinkScope&) = delete;
        ValueLinkScope& operator=(const ValueLinkScope&) = delete;
        ValueLinkScope(ValueLinkScope&&) = delete;
        ValueLinkScope& operator=(ValueLinkScope&&) = delete;

        /// Makes @p tables, which outlives the scope, the provider of `ByLevel` for the loads under this scope; it is read while a formula compiles and never kept
        void UseTables(const IValueTables& tables) {
            _tables = &tables;
        }

        /// The declaration of the innermost scope that has one, null when none has: an empty scope does not hide the declaration of the scope around it
        [[nodiscard]] static const ContextDecl* Current();

        /// The tables of the innermost scope that has some, null when none has: a scope without tables does not hide the ones around it
        [[nodiscard]] static const IValueTables* Tables();

        /// Gives the innermost scope a copy of @p declaration; the error text when no scope is open or that scope has a declaration already
        [[nodiscard]] static std::expected<void, String> Declare(const ContextDecl& declaration);

        /// The name of the document of the innermost scope that has one, empty when none has
        [[nodiscard]] static StringView DocumentName();

        /// @p message, ending with the document's name when a scope has one, so that an error read alone names the file
        [[nodiscard]] static String AtDocument(StringView message);

    private:
        [[nodiscard]] const ContextDecl* Declaration() const {
            return _external != nullptr ? _external : (_owned.has_value() ? &*_owned : nullptr);
        }

        const ContextDecl* _external = nullptr;
        const IValueTables* _tables = nullptr;
        Optional<ContextDecl> _owned;
        String _documentName;
        ValueLinkScope* _previous;
    };

}  // namespace BECore
