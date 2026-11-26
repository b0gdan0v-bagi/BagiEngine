#include "RemoteSessionWidget.h"

#include <BECore/Application/CommandLine.h>
#include <BECore/GameManager/CoreManager.h>
#include <Generated/RemoteSessionWidget.gen.hpp>
#include <chrono>

namespace BECore {

    RemoteSessionWidget::~RemoteSessionWidget() {
        _session.Detach();
    }

    bool RemoteSessionWidget::Initialize(IDeserializer& /*deserializer*/) {
        RemoteSessionRegistry& registry = RemoteSessionRegistry::GetInstance();
        _handle = registry.Register(_session);
        const Optional<StringView> address = CoreManager::GetCommandLine().FindValue(kAttachFlag);
        if (address && registry.TakeStartAttach()) {
            // A refused address leaves the editor on its own scene: the widget stays, so the Attach panel can try another
            if (const auto attached = _session.Attach(*address); !attached) {
                LOG_ERROR("Remote editor: cannot attach to '{}': {}", *address, attached.error());
            }
        }
        return true;
    }

    void RemoteSessionWidget::Update() {
        _session.Poll(std::chrono::steady_clock::now());
    }

}  // namespace BECore
