#pragma once

#include <BECore/Types/Types.h>
#include <type_traits>

namespace BECore {

    /**
     * @brief A text field of a reflected answer that points at text kept elsewhere: written as a string, never copied.
     *
     * Use it where the text already lives past the write of the answer (a method or error name of a table, the client's own request text, a
     * registry name); `ArenaString` is for text the handler makes. The view must outlive every write of the struct, which a local answer of
     * the handler satisfies for anything the handler holds; a text made inside the handler (a `Format` result) is copied instead.
     *
     * **Write-only.** It is a string for every writer (`Ser::DataAccessor<TextView>`, `TypeKind::String`: the same bytes and the same schema as a
     * `String` field), but a reader leaves it as it is: a view has no place to put what it reads. So a struct that holds one is an answer for
     * `WriteJson`, never a saved type and never an argument of a `BE_FUNCTION`, and no path of a root may reach it (its memory is no `String`).
     */
    class TextView {
    public:
        TextView() = default;
        explicit TextView(StringView text) : _text(text) {}

        /// A temporary `String` would dangle at the end of the statement
        template <typename TText>
        requires std::is_same_v<TText, String> explicit TextView(TText&&) = delete;

        [[nodiscard]] StringView View() const {
            return _text;
        }

        [[nodiscard]] const char* data() const {
            return _text.data();
        }

        [[nodiscard]] size_t size() const {
            return _text.size();
        }

        /// The property visitors (editor, paths) see no member: the text is not theirs to edit
        template <typename TVisitor>
        bool VisitProperties(TVisitor& /*visitor*/) {
            return false;
        }

    private:
        StringView _text;
    };

}  // namespace BECore
