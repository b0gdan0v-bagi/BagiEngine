#pragma once

#if BE_CONTROL_LINK

#include <Application/ControlLink.h>

namespace BECore {

    /// What the page's address asks for: nothing without `controlToken`, and nothing but one warning for a query that cannot be read
    [[nodiscard]] Optional<ControlLinkSettings> ReadPageControlLinkSettings();

    /// The link of this page, over the browser's WebSocket, with the methods of host files refused (RefuseHostFileMethods). Once the modules have
    /// registered their methods; main thread
    [[nodiscard]] UniquePtr<ControlLink> StartPageControlLink(ControlService& service, ControlLinkSettings settings);

}  // namespace BECore

#endif  // BE_CONTROL_LINK
