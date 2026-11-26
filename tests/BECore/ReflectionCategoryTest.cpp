#include "ReflectionCategoryTest.h"

#include <Common/Printers.h>
#include <Generated/ReflectionCategoryTest.gen.hpp>

namespace BECore::Tests {

    using namespace ReflectionCategoryTestTypes;

    TEST(ReflectionCategoryTest, TheOptionIsCarriedByTheStaticMetaAndNotInherited) {
        EXPECT_EQ(CategoryBase::GetStaticTypeMeta().category, "game-logic");
        EXPECT_TRUE(CategoryChild::GetStaticTypeMeta().category.empty());
    }

}  // namespace BECore::Tests
