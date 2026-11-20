#pragma once

#include <BECore/ProgramDomainData.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <cstdint>

namespace BECore::ProgramDomainData {

    // A document whose fields are values of the toy types of `ProgramDomainData.h`, to load formulas over a registered type from content. The .gen.hpp of this header is included in
    // ProgramDomainLoadTest.cpp only; a type is found by its simple name, so the names are unique among all test headers.

    struct DomainDocument {
        BE_CLASS(DomainDocument)

        BE_REFLECT_FIELD ContextDecl context;
        BE_REFLECT_FIELD Value<DomainBits256> blocked;
        BE_REFLECT_FIELD Value<DomainBits256> reach;
        BE_REFLECT_FIELD Value<int32_t> crowd = 7;
        BE_REFLECT_FIELD Value<bool> open = false;
        /// A type nobody registers: `expr` on it is refused
        BE_REFLECT_FIELD Value<DomainUnregistered> loose;
    };

}  // namespace BECore::ProgramDomainData
