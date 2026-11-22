#include "ContentCooker.h"

namespace BECore {

    ContentCookRegistry& ContentCookRegistry::GetInstance() {
        static ContentCookRegistry instance;
        return instance;
    }

    void ContentCookRegistry::Register(const ContentCooker& cooker) {
        ASSERT(Find(cooker.stem) == nullptr, "a stem has one cooker");
        if (Find(cooker.stem) != nullptr) {
            return;
        }
        _cookers.push_back(cooker);
    }

    const ContentCooker* ContentCookRegistry::Find(StringView stem) const {
        for (const ContentCooker& cooker : _cookers) {
            if (cooker.stem == stem) {
                return &cooker;
            }
        }
        return nullptr;
    }

    void AppendFindings(const Vector<DeserializeError>& messages, Vector<String>& findings) {
        for (const DeserializeError& message : messages) {
            findings.push_back(message.path.empty() ? message.errorMessage : Format("{}: {}", message.path, message.errorMessage));
        }
    }

    void AppendFindings(const IDeserializer& reader, Vector<String>& findings) {
        AppendFindings(reader.GetErrors(), findings);
        AppendFindings(reader.GetWarnings(), findings);
    }

}  // namespace BECore
