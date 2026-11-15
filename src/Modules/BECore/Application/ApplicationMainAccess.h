#pragma once

namespace BECore {
    /**
     * Key type: only main.cpp and the classes derived from it (for example SDL3ApplicationFabric) may call the methods of Application.
     */
    class ApplicationMainAccess {};
}  // namespace BECore
