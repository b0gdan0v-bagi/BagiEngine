#include "RmlUiSystemInterface.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>

namespace BECore {

    double RmlUiSystemInterface::GetElapsedTime() {
        return CoreManager::GetTime().GetTotalTime();
    }

    bool RmlUiSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
        const StringView line(message.data(), message.size());
        switch (type) {
            case Rml::Log::LT_ERROR:
            case Rml::Log::LT_ASSERT:
                LOG_ERROR("RmlUi: {}", line);
                break;
            case Rml::Log::LT_WARNING:
                LOG_WARNING("RmlUi: {}", line);
                break;
            case Rml::Log::LT_INFO:
            case Rml::Log::LT_ALWAYS:
                LOG_INFO("RmlUi: {}", line);
                break;
            default:
                LOG_DEBUG("RmlUi: {}", line);
                break;
        }
        return true;
    }

}  // namespace BECore
