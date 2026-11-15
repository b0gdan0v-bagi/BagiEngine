#include "AssetPurposeTest.h"

#include <Common/Printers.h>
#include <Generated/AssetPurposeTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace AssetPurposeTestTypes;

    TEST(AssetPurpose, TheOptionIsCarriedByTheStaticMetaAndTheTraits) {
        EXPECT_EQ(PurposeSimProbe::GetStaticTypeMeta().purpose, AssetPurpose::Sim);
        EXPECT_EQ(PurposeVisualProbe::GetStaticTypeMeta().purpose, AssetPurpose::Visual);
        EXPECT_EQ(PurposeEditorProbe::GetStaticTypeMeta().purpose, AssetPurpose::Editor);
        EXPECT_EQ(ReflectionTraits<PurposeSimProbe>::purpose, AssetPurpose::Sim);
        EXPECT_EQ(ReflectionTraits<PurposeVisualProbe>::purpose, AssetPurpose::Visual);
        EXPECT_EQ(ReflectionTraits<PurposeEditorProbe>::purpose, AssetPurpose::Editor);
    }

    TEST(AssetPurpose, AClassWithoutTheOptionIsCommon) {
        EXPECT_EQ(PurposeCommonProbe::GetStaticTypeMeta().purpose, AssetPurpose::Common);
        EXPECT_EQ(ReflectionTraits<PurposeCommonProbe>::purpose, AssetPurpose::Common);
    }

    TEST(AssetPurpose, ThePurposeIsReadThroughTheVirtualMeta) {
        const PurposeVisualProbe probe;
        EXPECT_EQ(probe.GetTypeMeta().purpose, AssetPurpose::Visual);
    }

    TEST(AssetPurpose, AMetaMadeByHandIsCommon) {
        constexpr ClassMeta meta{1, "X"};
        EXPECT_EQ(meta.purpose, AssetPurpose::Common);
    }

    TEST(AssetPurpose, NamesParseBack) {
        EXPECT_EQ(PurposeName(AssetPurpose::Sim), "sim");
        EXPECT_EQ(PurposeName(AssetPurpose::Visual), "visual");
        EXPECT_EQ(PurposeName(AssetPurpose::Editor), "editor");
        EXPECT_EQ(TryParsePurpose("sim"), AssetPurpose::Sim);
        EXPECT_EQ(TryParsePurpose("visual"), AssetPurpose::Visual);
        EXPECT_EQ(TryParsePurpose("editor"), AssetPurpose::Editor);
        EXPECT_FALSE(TryParsePurpose("common").has_value());
        EXPECT_FALSE(TryParsePurpose("Sim").has_value());
        EXPECT_FALSE(TryParsePurpose("").has_value());
    }

    TEST(AssetPurpose, IsAllowedInTable) {
        constexpr AssetPurpose kTypes[] = {AssetPurpose::Common, AssetPurpose::Sim, AssetPurpose::Visual, AssetPurpose::Editor};
        constexpr AssetPurpose kParts[] = {AssetPurpose::Sim, AssetPurpose::Visual, AssetPurpose::Editor};
        for (const AssetPurpose type : kTypes) {
            for (const AssetPurpose part : kParts) {
                const bool expected = type == AssetPurpose::Common || type == part;
                EXPECT_EQ(IsAllowedIn(type, part), expected) << PurposeName(type).data() << " in " << PurposeName(part).data();
            }
        }
    }

}  // namespace BECore::Tests
