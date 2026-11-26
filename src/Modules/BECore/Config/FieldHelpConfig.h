#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// The description of one reflected field of a component
    struct FieldHelpEntry {
        BE_CLASS(FieldHelpEntry)

        /// The class name of a component, or of a parent class of the ones it describes
        BE_REFLECT_FIELD BE_FIELD_ATTRS(required) PoolString component;
        /// The serialized name of the field
        BE_REFLECT_FIELD BE_FIELD_ATTRS(required) PoolString field;
        BE_REFLECT_FIELD String text;
    };

    /// The content of a field help file (`FieldHelpConfig.xml`, `GameFieldHelpConfig.xml`): editor data, here so that every build's config checks can read it
    struct FieldHelpConfig {
        BE_CLASS(FieldHelpConfig)

        BE_REFLECT_FIELD Vector<FieldHelpEntry> entries;
    };

}  // namespace BECore
