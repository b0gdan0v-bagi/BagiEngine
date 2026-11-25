#include <BECore/PanelModelProbe.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Generated/PanelModelProbe.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/PanelAssets/PanelSlots.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    TEST(PanelModelTest, TheFileChangesCommandsAreFoundByText) {
        for (const StringView id : {"fileChanges.rescan", "fileChanges.applyAll", "fileChanges.apply"}) {
            const EditorCommand* command = FindEditorCommand(id);
            ASSERT_NE(command, nullptr) << String(id);
            EXPECT_EQ(command->id.ToStringView(), id);
            EXPECT_FALSE(command->defaultChord.has_value()) << String(id);
        }
    }

    TEST(PanelModelTest, AnUnknownCommandNameIsNotFoundAndNotInterned) {
        const String name = FreshText("panel.command");

        EXPECT_EQ(FindEditorCommand(StringView(name)), nullptr);
        EXPECT_FALSE(PoolString::Find(StringView(name)).has_value());
    }

    TEST(PanelModelTest, AModelIsMadeByItsClassName) {
        EXPECT_TRUE(IsKnownPanelModel("ProbePanelModel"));
        const IntrusivePtr<IPanelModel> model = CreatePanelModel("ProbePanelModel");
        ASSERT_TRUE(model);

        model->Update();
        EXPECT_TRUE(model->RunCommand("probe.run", "arg"));
        EXPECT_FALSE(model->RunCommand("probe.other", ""));

        // The default object is the model itself, found again through its most derived type
        const TypedRef object = model->GetObject();
        ASSERT_TRUE(object);
        EXPECT_EQ(object.Address(), static_cast<void*>(static_cast<ProbePanelModel*>(model.Get())));
    }

    TEST(PanelModelTest, AnUnknownModelNameIsNotKnownAndNotInterned) {
        const String name = FreshText("PanelModelNobody");

        EXPECT_FALSE(IsKnownPanelModel(name));
        EXPECT_FALSE(CreatePanelModel(name));
        EXPECT_FALSE(PoolString::Find(StringView(name)).has_value());
    }

    TEST(PanelModelTest, ASlotRegistersAndItsHandleUnregistersIt) {
        PanelSlots slots;
        int calls = 0;
        {
            const PanelSlotHandle handle = slots.Register("chart", [&calls] { ++calls; });
            PanelSlotDraw* draw = slots.Find("chart");
            ASSERT_NE(draw, nullptr);
            (*draw)();
            EXPECT_EQ(calls, 1);
            EXPECT_EQ(slots.Find("other"), nullptr);
        }
        EXPECT_EQ(slots.Find("chart"), nullptr);
    }

    TEST(PanelModelTest, ASlotRegisteredAgainReplacesTheDrawerAndTheOlderHandleRemovesNothing) {
        PanelSlots slots;
        int which = 0;
        PanelSlotHandle first = slots.Register("chart", [&which] { which = 1; });
        const PanelSlotHandle second = slots.Register("chart", [&which] { which = 2; });

        first.Reset();

        PanelSlotDraw* draw = slots.Find("chart");
        ASSERT_NE(draw, nullptr);
        (*draw)();
        EXPECT_EQ(which, 2);
    }

}  // namespace BECore::Tests
