#include "RmlUiSystemInterface.h"

#include <BECore/GameManager/CoreManager.h>

namespace BECore {

    double RmlUiSystemInterface::GetElapsedTime() {
        return CoreManager::GetTime().GetTotalTime();
    }

    bool RmlUiSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
        const eastl::string line = "RmlUi: {}"_format(FromStd(message));
        switch (type) {
            case Rml::Log::LT_ERROR:
            case Rml::Log::LT_ASSERT:
                LOG_ERROR(line);
                break;
            case Rml::Log::LT_WARNING:
                LOG_WARNING(line);
                break;
            case Rml::Log::LT_INFO:
            case Rml::Log::LT_ALWAYS:
                LOG_INFO(line);
                break;
            default:
                LOG_DEBUG(line);
                break;
        }
        return true;
    }

}  // namespace BECore
