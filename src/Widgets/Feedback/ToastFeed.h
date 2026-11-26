#pragma once

#include <BECore/Types/Types.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <Widgets/Feedback/ToastList.h>
#include <cstdint>

namespace BECore {

    class ControlActivity;

    /**
     * @brief Fills a ToastList from what happens in the editor: EditorToastEvent, a saved or reloaded config, a failed reload, and a control API
     * client that connected or disconnected. No ImGui.
     *
     * The events arrive as they are emitted; the connections are read from the control journal by Update(). Main thread only.
     */
    class ToastFeed : public SubscriptionHolder {
    public:
        /// Starts listening. What @p activity holds already is old news; it must outlive the feed
        void Connect(const ControlActivity& activity);

        /// Ages the toasts by @p seconds, then adds those of the connections recorded since the last call. A call with nothing new allocates nothing
        void Update(float seconds);

        [[nodiscard]] const ToastList& GetList() const {
            return _list;
        }

    private:
        void ReadActivity();

        void OnToast(const EditorToastEvent& event);
        void OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event);
        void OnReloadFailed(const ConfigEvents::ConfigReloadFailedEvent& event);

        ToastList _list;
        const ControlActivity* _activity = nullptr;
        uint64_t _seenVersion = 0;
        uint64_t _nextSequence = 0;  ///< the first entry of the journal not read yet
    };

}  // namespace BECore
