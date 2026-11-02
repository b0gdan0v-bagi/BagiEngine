#include "ConfigFactory.h"

namespace BECore {

    void LogDeserializeErrors(eastl::string_view configName, const IDeserializer& deserializer) {
        for (const DeserializeError& error : deserializer.GetErrors()) {
            // A document that fails to parse has no path, and the line is 0 where the format has none (binary)
            const eastl::string path = error.path.ToStringView().empty() ? eastl::string{} : Format("{}: ", error.path.ToStringView());
            const eastl::string line = error.line != 0 ? Format(" (line {})", error.line) : eastl::string{};
            LOG_ERROR(Format("Config '{}': {}{}{}", configName, path, error.errorMessage.ToStringView(), line).c_str());
        }
    }

}  // namespace BECore
