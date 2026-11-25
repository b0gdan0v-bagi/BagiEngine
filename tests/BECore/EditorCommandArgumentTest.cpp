#include <Common/Printers.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Keeps what the subscriber of a panel would see
        class CommandRecorder : public SubscriptionHolder {
        public:
            CommandRecorder() {
                Subscribe<EditorCommandEvent, &CommandRecorder::OnCommand>(this);
            }

            Vector<PoolString> ids;
            Vector<String> arguments;

        private:
            void OnCommand(const EditorCommandEvent& event) {
                ids.push_back(event.commandId);
                arguments.push_back(event.argument);
            }
        };

    }  // namespace

    TEST(EditorCommandArgumentTest, EmitWithArgumentReachesSubscriber) {
        CommandRecorder recorder;
        EditorCommandEvent::Emit("fileChanges.apply"_intern, String("assets/a.png"));
        ASSERT_EQ(recorder.ids.size(), 1u);
        EXPECT_EQ(recorder.ids[0], "fileChanges.apply"_intern);
        EXPECT_EQ(recorder.arguments[0], "assets/a.png");
    }

    TEST(EditorCommandArgumentTest, EmitWithoutArgumentGivesEmptyOne) {
        CommandRecorder recorder;
        EditorCommandEvent::Emit(EditorCommandIds::kViewConsole);
        ASSERT_EQ(recorder.ids.size(), 1u);
        EXPECT_EQ(recorder.ids[0], EditorCommandIds::kViewConsole);
        EXPECT_TRUE(recorder.arguments[0].empty());
    }

    TEST(EditorCommandArgumentTest, TableCommandIsStillFound) {
        const EditorCommand* command = FindEditorCommand(EditorCommandIds::kViewConsole);
        ASSERT_NE(command, nullptr);
        EXPECT_EQ(command->id, EditorCommandIds::kViewConsole);
    }

}  // namespace BECore::Tests
