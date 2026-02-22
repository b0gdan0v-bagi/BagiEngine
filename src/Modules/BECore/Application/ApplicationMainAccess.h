#pragma once

namespace BECore {
    /**
     * Класс-ключ для доступа к методам Application только из main.cpp и классов,
     * которые наследуются от ApplicationMainAccess (например, SDL3ApplicationFabric).
     */
    class ApplicationMainAccess {};
}  // namespace BECore

