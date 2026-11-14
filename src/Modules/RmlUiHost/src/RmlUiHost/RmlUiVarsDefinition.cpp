#include "RmlUiVarsDefinition.h"

#include <BECore/GameManager/CoreManager.h>
#include <RmlUi/Core/Variant.h>

namespace BECore {

    bool RmlUiVarsDefinition::Scalar::Get(void* ptr, Rml::Variant& variant) {
        const StringView value = CoreManager::GetUIVars().Get(*static_cast<const PoolString*>(ptr));
        variant = Rml::String(value.data(), value.size());
        return true;
    }

    Rml::DataVariable RmlUiVarsDefinition::Child(void* /*ptr*/, const Rml::DataAddressEntry& address) {
        if (address.name.empty()) {
            return {};
        }
        const String name(address.name.data(), address.name.size());
        auto found = _names.find(name);
        if (found == _names.end()) {
            found = _names.emplace(name, PoolString::Intern(name)).first;
        }
        return Rml::DataVariable(&_scalar, &found->second);
    }

}  // namespace BECore
