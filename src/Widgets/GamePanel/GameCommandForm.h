#pragma once

#include <BECore/Types/Types.h>
#include <Games/GameEditorTools.h>
#include <cstddef>
#include <expected>

namespace BECore {

    /// Longest text a field of the command form takes, in bytes
    inline constexpr size_t kMaxCommandFieldLength = 4095;

    /// What the form holds for one parameter: the typed text, or the switch of a `Boolean`
    struct GameCommandFormField {
        GameCommandParam param;
        char text[kMaxCommandFieldLength + 1] = {};
        bool flag = false;
    };

    /**
     * @brief What a person types for the parameters of a game command, and the JSON arguments made of it. No ImGui.
     *
     * It keeps copies of the parameters, so it outlives the command it was made for.
     */
    class GameCommandForm {
    public:
        explicit GameCommandForm(Span<const GameCommandParam> params);

        /// In the order of the parameters; the form's owner writes into them
        [[nodiscard]] Vector<GameCommandFormField>& GetFields() {
            return _fields;
        }

        /// The arguments as the text of one JSON object, or one line that says which field is wrong. An empty optional field is left out (a number or
        /// a JSON field of spaces only is empty too); a switch is always written. Numbers are JSON numbers: no `+`, no leading zero
        [[nodiscard]] std::expected<String, String> BuildArguments() const;

    private:
        Vector<GameCommandFormField> _fields;
    };

}  // namespace BECore
