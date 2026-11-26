#include "ControlClientLabel.h"

#include <BECore/Control/ControlClient.h>

namespace BECore {

    namespace {

        constexpr StringView kEngineClientText = "engine";

    }  // namespace

    StringView ControlClientLabel(const ControlClient* client, Array<char, kControlClientLabelSize>& buffer) {
        if (client == nullptr) {
            return kEngineClientText;
        }
        if (!client->GetName().empty()) {
            return StringView(client->GetName());
        }
        const FormatResult written = FormatToN(buffer.data(), buffer.size(), "client {}", client->GetId());
        return StringView(buffer.data(), written.truncated ? buffer.size() : written.size);
    }

}  // namespace BECore
