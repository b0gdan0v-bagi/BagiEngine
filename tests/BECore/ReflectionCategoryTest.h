#pragma once

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/TypeTraits.h>

namespace BECore::Tests {

    namespace ReflectionCategoryTestTypes {

        /// A class that names a category.
        struct CategoryBase {
            BE_CLASS(CategoryBase, CATEGORY = game - logic)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        /// Derived class that names none.
        struct CategoryChild : CategoryBase {
            BE_CLASS(CategoryChild)
        };

    }  // namespace ReflectionCategoryTestTypes

}  // namespace BECore::Tests
