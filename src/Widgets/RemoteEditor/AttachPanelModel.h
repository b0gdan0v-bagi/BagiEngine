#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/RemoteEditor/RemoteSession.h>

namespace BECore {

    /// Where a game started with its default control port listens
    inline constexpr StringView kDefaultAttachAddress = "ws://127.0.0.1:8765";

    /**
     * @brief What `AttachPanel.xml` binds to
     *
     * The input writes `address` at every change; the model reads it when Attach is pressed. Every other field is the model's.
     */
    struct AttachPanelView {
        BE_CLASS(AttachPanelView)

        BE_REFLECT_FIELD String address = String(kDefaultAttachAddress);
        /// The state of the link in a sentence, with the address and, for a lost link, the reason
        BE_REFLECT_FIELD String status;
        /// The link is lost or the program does not answer: the status is a warning
        BE_REFLECT_FIELD bool troubled = false;
        /// Why the last Attach did not start; empty when it did
        BE_REFLECT_FIELD String error;
        BE_REFLECT_FIELD bool hasError = false;
        /// The editor has a session (`RemoteSessionWidget`)
        BE_REFLECT_FIELD bool canAttach = false;
        /// The editor is attached, connecting or retrying
        BE_REFLECT_FIELD bool canDetach = false;
    };

    /**
     * @brief The model of the Attach panel: starts and ends the link of the registered `RemoteSession` and says how it is
     *
     * It does not poll the session (`RemoteSessionWidget` does). The sentences are made again only when the state, the stall, the address or
     * the reason changed.
     *
     * Commands (`EditorCommandIds`): `remote.attach` (the button and the input's `submitCommand`) and `remote.detach`.
     */
    class AttachPanelModel : public IPanelModel {
        BE_CLASS(AttachPanelModel)
    public:
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] const AttachPanelView& GetView() const {
            return _view;
        }
        [[nodiscard]] AttachPanelView& GetView() {
            return _view;
        }

    private:
        void Attach();
        void SetError(StringView error);
        /// Makes `status`, `troubled` and the buttons from @p session
        void Describe(const RemoteSession& session);

        AttachPanelView _view;
        // What the sentence was made from; nullopt before the first one and while there is no session
        Optional<RemoteSessionState> _shownState;
        bool _shownStalled = false;
        String _shownAddress;
        String _shownReason;
    };

}  // namespace BECore
