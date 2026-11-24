#include <BECore/Scene/SceneNode.h>
#include <Stress/Components/StressEntitiesComponent.h>
#include <Stress/Components/StressRunComponent.h>
#include <Stress/Components/StressSpritesComponent.h>
#include <Stress/Components/StressUiComponent.h>
#include <gtest/gtest.h>

#ifdef STRESS_WITH_RMLUI
#include <Stress/RmlUi/StressRmlUiComponent.h>
#endif

namespace BECore::Stress::Tests {

    /// Components that never draw stay out of the scene's draw list
    TEST(StressHasOnDrawTest, TheSceneComponentsDrawNothingThemselves) {
        EXPECT_FALSE(StressEntitiesComponent().HasOnDraw());
        EXPECT_FALSE(StressSpritesComponent().HasOnDraw());
        EXPECT_FALSE(StressUiComponent().HasOnDraw());
        EXPECT_FALSE(StressRunComponent().HasOnDraw());
#ifdef STRESS_WITH_RMLUI
        EXPECT_FALSE(StressRmlUiComponent().HasOnDraw());
#endif
    }

}  // namespace BECore::Stress::Tests
