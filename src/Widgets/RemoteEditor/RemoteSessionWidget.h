#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/RemoteEditor/RemoteSession.h>
#include <Widgets/RemoteEditor/RemoteSessionRegistry.h>

namespace BECore {

    /// `--attach=<address>`: the editor attaches to the control endpoint at this address as it starts
    inline constexpr StringView kAttachFlag = "--attach";

    /**
     * @brief The owner of the editor's `RemoteSession`: polls it once a frame and registers it in `RemoteSessionRegistry`
     *
     * Draws nothing. Listed in `WidgetsConfig.xml` before the panels, so they read what this frame's poll took. The link ends with the widget,
     * so a switch to Game mode detaches.
     */
    class RemoteSessionWidget : public IWidget {
        BE_CLASS(RemoteSessionWidget)
    public:
        RemoteSessionWidget() = default;
        explicit RemoteSessionWidget(RoomTransportFactory transportFactory) : _session(std::move(transportFactory)) {}
        ~RemoteSessionWidget() override;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void Draw() override {}

    private:
        RemoteSession _session;
        /// Declared last: it unregisters before the session dies
        RemoteSessionHandle _handle;
    };

}  // namespace BECore
