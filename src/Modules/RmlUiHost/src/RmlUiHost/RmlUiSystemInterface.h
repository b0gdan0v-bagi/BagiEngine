#pragma once

#include <RmlUi/Core/SystemInterface.h>

namespace BECore {

    /// Rml::SystemInterface over the engine: the game clock (CoreManager::GetTime()) and the logger
    class RmlUiSystemInterface final : public Rml::SystemInterface {
    public:
        double GetElapsedTime() override;
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    };

}  // namespace BECore
