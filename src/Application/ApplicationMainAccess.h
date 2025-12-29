#pragma once

#include <Core/Utils/PassKey.h>

namespace Core {
    /**
     * Класс-ключ для доступа к методам Application только из main.cpp и классов,
     * которые наследуются от ApplicationMainAccess (например, ApplicationSDLFabric).
     */
    class ApplicationMainAccess {};

    // Forward declaration для ApplicationSDLFabric
    class ApplicationSDLFabric;

    // Специализация PassKey для ApplicationMainAccess, разрешающая доступ
    // для ApplicationMainAccess и его наследников (ApplicationSDLFabric)
    template <>
    class PassKey<ApplicationMainAccess> {
        friend ApplicationMainAccess;
        friend ApplicationSDLFabric;

    public:
        PassKey() = default;
        PassKey(const PassKey&) = default;
        PassKey& operator=(const PassKey&) = default;
        PassKey(PassKey&&) = default;
        PassKey& operator=(PassKey&&) = default;
    };
}  // namespace Core

