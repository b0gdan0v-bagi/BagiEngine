#include <BECore/Cook/ContentCook.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/PanelAssets/PanelInterpreter.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kPanelStemSuffix = "Panel";

        Path PanelsFolder() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels";
        }

        bool IsKnownCommand(StringView name) {
            return FindEditorCommand(name) != nullptr;
        }

        /// The first element of the tree that the interpreter does not draw, or null
        const PanelElement* FindUndrawn(const Vector<IntrusivePtr<PanelElement>>& elements) {
            for (const IntrusivePtr<PanelElement>& element : elements) {
                if (!PanelInterpreter::CanDraw(*element)) {
                    return element.Get();
                }
                if (const PanelElement* inner = FindUndrawn(element->elements)) {
                    return inner;
                }
            }
            return nullptr;
        }

    }  // namespace

    /// The panel files the engine ships: a typo in one is found here, not by the owner opening the panel
    TEST(PanelAssetShippedTest, EveryShippedPanelLoadsWithNoIssueUnderTheRealPredicatesAndOnlyUsesKindsTheInterpreterDraws) {
        String error;
        const Vector<Path> files = ListContentFiles(PanelsFolder(), error);
        ASSERT_TRUE(error.empty()) << error.c_str();
        ASSERT_FALSE(files.empty());

        const FunctionRef<bool(StringView)> knownCommand(IsKnownCommand);
        const FunctionRef<bool(StringView)> knownModel(IsKnownPanelModel);
        PanelValidationContext context;
        context.isKnownCommand = knownCommand;
        context.isKnownModel = knownModel;
        for (const Path& file : files) {
            const String name(file.View());
            const String stem(PathStem(file.View()));
            EXPECT_TRUE(StringView(stem).ends_with(kPanelStemSuffix)) << name.c_str() << ": a panel's file stem ends in Panel, so it cannot be a config's name";

            const PanelLoadResult loaded = LoadPanelAsset(ReadFile(PanelsFolder() / file.View()), context);
            for (const PanelIssue& issue : loaded.issues) {
                ADD_FAILURE() << name.c_str() << ": " << issue.where.c_str() << ": " << issue.message.c_str();
            }
            ASSERT_TRUE(loaded.asset) << name.c_str();
            const PanelElement* undrawn = FindUndrawn(loaded.asset->elements);
            EXPECT_EQ(undrawn, nullptr) << name.c_str() << ": the element " << (undrawn != nullptr ? undrawn->id.c_str() : "") << " is of a kind the interpreter does not draw";
        }
    }

}  // namespace BECore::Tests
