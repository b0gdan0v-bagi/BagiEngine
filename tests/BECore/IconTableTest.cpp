#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Components/ChangeToRandomColorDebugComponent.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/RenderProbeComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/TextComponent.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/Icons/IconsLucide.h>
#include <cstring>

namespace BECore::Tests {

    TEST(IconTableTest, RowsAreSortedByNameWithoutDuplicates) {
        const auto table = GetIconTable();
        ASSERT_FALSE(table.empty());
        for (size_t i = 1; i < table.size(); ++i) {
            EXPECT_LT(table[i - 1].name, table[i].name) << "at row " << i;
        }
    }

    TEST(IconTableTest, EveryGlyphIsNonEmptyAndEveryNameIsPlain) {
        for (const IconEntry& entry : GetIconTable()) {
            ASSERT_NE(entry.glyph, nullptr) << entry.name.data();
            EXPECT_GT(std::strlen(entry.glyph), 0u) << entry.name.data();
            EXPECT_FALSE(entry.name.empty());
            for (const char c : entry.name) {
                EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') << entry.name.data();
            }
        }
    }

    TEST(IconTableTest, FindsEveryRowAndNothingElse) {
        for (const IconEntry& entry : GetIconTable()) {
            EXPECT_EQ(FindIcon(entry.name), entry.glyph) << entry.name.data();
        }
        EXPECT_STREQ(FindIcon("save"), ICON_LC_SAVE);
        EXPECT_EQ(FindIcon("no-such-icon"), nullptr);
        EXPECT_EQ(FindIcon(""), nullptr);
        EXPECT_EQ(FindIcon("sav"), nullptr);
        EXPECT_EQ(FindIcon("savee"), nullptr);
    }

    TEST(IconTableTest, TheGenericNodeIconIsABox) {
        EXPECT_STREQ(GetNodeIcon(), ICON_LC_BOX);
        EXPECT_EQ(FindIcon("box"), GetNodeIcon());
    }

    TEST(IconTableTest, EveryIconOfTheEnginesComponentsResolves) {
        const ClassMeta metas[] = {
            TransformComponent::GetStaticTypeMeta(), SpriteRendererComponent::GetStaticTypeMeta(), QuadRendererComponent::GetStaticTypeMeta(), TextComponent::GetStaticTypeMeta(),
            UICanvasComponent::GetStaticTypeMeta(),  ClickableComponent::GetStaticTypeMeta(),      RenderProbeComponent::GetStaticTypeMeta(),  ChangeToRandomColorDebugComponent::GetStaticTypeMeta(),
        };
        for (const ClassMeta& meta : metas) {
            EXPECT_FALSE(meta.icon.empty()) << meta.typeName.data();
            EXPECT_NE(FindIcon(meta.icon), nullptr) << meta.typeName.data() << " names '" << meta.icon.data() << "'";
        }
    }

    TEST(IconTableTest, IconOfClassFallsBackToTheGenericIcon) {
        EXPECT_EQ(IconOfClass(TransformComponent::GetStaticTypeMeta()), FindIcon("move"));

        ClassMeta none{1, "NoIcon"};
        EXPECT_EQ(IconOfClass(none), GetNodeIcon());

        ClassMeta unknown{2, "UnknownIcon", "no-such-icon"};
        EXPECT_EQ(IconOfClass(unknown), GetNodeIcon());
        EXPECT_EQ(IconOfClass(unknown), GetNodeIcon());  // the second call finds the name already reported
    }

    TEST(IconTableTest, ANodeShowsTheIconOfItsFirstComponentThatNamesOne) {
        EngineEnvironment::EnsureCoreServices();
        auto scene = New<Scene>();
        scene->Initialize();
        auto node = scene->CreateNode("Icons"_intern, nullptr);
        EXPECT_EQ(IconOfNode(*node), GetNodeIcon());

        node->AddComponent(New<TransformComponent>());
        EXPECT_EQ(IconOfNode(*node), FindIcon("move"));

        node->AddComponent(New<SpriteRendererComponent>());
        EXPECT_EQ(IconOfNode(*node), FindIcon("move"));
    }

}  // namespace BECore::Tests
