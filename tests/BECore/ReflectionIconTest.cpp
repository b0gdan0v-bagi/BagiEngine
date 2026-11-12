#include "ReflectionIconTest.h"

#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <Common/Printers.h>
#include <Generated/ReflectionIconTest.gen.hpp>

namespace BECore::Tests {

    using namespace ReflectionIconTestTypes;

    TEST(ReflectionIconTest, TheOptionIsCarriedByTheStaticMeta) {
        EXPECT_EQ(IconBase::GetStaticTypeMeta().icon, "image");
        EXPECT_EQ(IconChild::GetStaticTypeMeta().icon, "grid-3x3");
    }

    TEST(ReflectionIconTest, TheOptionIsReadThroughABasePointer) {
        IconChild child;
        const IconBase* base = &child;
        EXPECT_EQ(base->GetTypeMeta().icon, "grid-3x3");
    }

    TEST(ReflectionIconTest, AClassWithoutTheOptionHasAnEmptyIcon) {
        EXPECT_TRUE(NoIcon::GetStaticTypeMeta().icon.empty());
    }

    TEST(ReflectionIconTest, TheEnginesComponentsNameTheirIcons) {
        EXPECT_EQ(TransformComponent::GetStaticTypeMeta().icon, "move");
        EXPECT_EQ(SpriteRendererComponent::GetStaticTypeMeta().icon, "image");
    }

}  // namespace BECore::Tests
