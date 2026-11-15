#include "TypeRegistryTest.h"

#include <BECore/Reflection/IDeserializer.h>
#include <Generated/TypeRegistryTest.gen.hpp>

namespace BECore::RegistryTestData {

    bool RegMethods::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    RegNode* RegMethods::Find(int32_t /*id*/) const {
        return nullptr;
    }

}  // namespace BECore::RegistryTestData
