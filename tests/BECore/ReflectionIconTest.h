#pragma once

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/TypeTraits.h>

namespace BECore::Tests {

    namespace ReflectionIconTestTypes {

        /// A class that names a Lucide icon.
        struct IconBase {
            BE_CLASS(IconBase, ICON = image)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        /// Derived class: the key is case-insensitive and the spaces around it do not matter.
        struct IconChild : IconBase {
            BE_CLASS(IconChild, icon = grid - 3x3)
        };

        /// A class without the option.
        struct NoIcon {
            BE_CLASS(NoIcon)
        };

    }  // namespace ReflectionIconTestTypes

}  // namespace BECore::Tests
