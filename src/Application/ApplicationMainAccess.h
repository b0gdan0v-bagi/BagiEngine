#pragma once

namespace Core {
    /**
     * Класс-ключ для доступа к методам Application только из main.cpp и классов,
     * которые наследуются от ApplicationMainAccess (например, ApplicationSDLFabric).
     */
    class ApplicationMainAccess {};
}  // namespace Core

