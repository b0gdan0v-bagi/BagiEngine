#pragma once

#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/GamePanel/GameCommandForm.h>
#include <cstdint>

namespace BECore {

    class GameEditorTools;

    /**
     * @brief Runs the game commands the editor asks for (EditorCommandEvent with a control method of the registry).
     *
     * A command without parameters runs inside the event; one with parameters opens a modal form first. The result or the error is a toast and a log line.
     * A command may switch the game inside its call. The form belongs to the registry it was opened on: it closes when the registry's version moves.
     */
    class GameCommandRunner : public SubscriptionHolder {
    public:
        /// Starts to listen to EditorCommandEvent
        void Connect();

        /// The registry whose commands are run; null: there is none and every event is ignored
        void SetTools(GameEditorTools* tools) {
            _tools = tools;
        }

        /// Draws the form while one is open: once per frame, always inside the same window (a popup belongs to the ID stack it is opened in)
        void Draw();

    private:
        struct PendingForm {
            String method;
            String title;
            String summary;
            String popupLabel;
            String error;
            uint64_t version = 0;
            GameCommandForm form;
            bool openRequested = true;
        };

        void OnCommand(const EditorCommandEvent& event);
        /// Runs the command of @p method with @p arguments (the text of a JSON object) and tells how it ended
        void Run(StringView method, StringView arguments);

        GameEditorTools* _tools = nullptr;
        Optional<PendingForm> _form;
    };

}  // namespace BECore
