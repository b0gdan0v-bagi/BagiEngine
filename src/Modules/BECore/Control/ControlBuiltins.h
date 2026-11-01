#pragma once

namespace BECore {

    class ControlService;

    /**
     * @brief Registers the methods that need nothing but the core services.
     *
     * | Method | Result |
     * |--------|--------|
     * | ping   | {"pong":true,"protocol":ControlService::kProtocolVersion} |
     * | status | {"frame","totalTime","deltaTime","fixedDeltaTime"} of CoreManager::GetTime() |
     * | quit   | {}; then ApplicationEvents::QuitEvent, so the application stops at the end of the frame. Bypasses the barrier |
     *
     * Registering twice replaces the handlers, so calling this again is harmless.
     */
    void RegisterControlBuiltins(ControlService& service);

}  // namespace BECore
