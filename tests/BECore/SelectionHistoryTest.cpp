// The history of the primary selections that Alt+Left and Alt+Right step through, and the chords against a text field that has the keyboard.

#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/SceneEditor/SelectionHistory.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <initializer_list>

namespace BECore::Tests {

    namespace {

        class SelectionHistoryTest : public ::testing::Test {
        protected:
            void Select(std::initializer_list<uint64_t> ids) {
                for (const uint64_t id : ids) {
                    _history.Record(id);
                    _selected = id;
                }
            }

            /// One step, whose answer becomes the selection and is told back as the widget tells it every frame; 0 leaves the selection
            uint64_t Back() {
                const auto isAlive = [this](uint64_t id) { return IsAlive(id); };
                return Take(_history.Back(_selected, isAlive));
            }

            uint64_t Forward() {
                const auto isAlive = [this](uint64_t id) { return IsAlive(id); };
                return Take(_history.Forward(_selected, isAlive));
            }

            void Kill(uint64_t id) {
                _dead.push_back(id);
            }

            SelectionHistory _history;
            uint64_t _selected = 0;
            Vector<uint64_t> _dead;

        private:
            uint64_t Take(uint64_t id) {
                if (id != 0) {
                    _selected = id;
                    _history.Record(id);
                }
                return id;
            }

            [[nodiscard]] bool IsAlive(uint64_t id) const {
                return eastl::find(_dead.begin(), _dead.end(), id) == _dead.end();
            }
        };

        TEST_F(SelectionHistoryTest, StepsBackAndForwardThroughTheSelections) {
            Select({1, 2, 3});
            EXPECT_EQ(Back(), 2u);
            EXPECT_EQ(Back(), 1u);
            EXPECT_EQ(Back(), 0u);
            EXPECT_EQ(Forward(), 2u);
            EXPECT_EQ(Forward(), 3u);
            EXPECT_EQ(Forward(), 0u);
        }

        TEST_F(SelectionHistoryTest, ANewSelectionDropsWhatWasAhead) {
            Select({1, 2, 3});
            EXPECT_EQ(Back(), 2u);
            Select({4});
            EXPECT_EQ(Forward(), 0u);
            EXPECT_EQ(Back(), 2u);
            EXPECT_EQ(Back(), 1u);
        }

        TEST_F(SelectionHistoryTest, ARepeatAndNothingAreNotRecorded) {
            Select({1, 2, 2, 0, 2});
            EXPECT_EQ(Back(), 1u);
            Select({0, 1});
            // Neither dropped what is ahead
            EXPECT_EQ(Forward(), 2u);
        }

        TEST_F(SelectionHistoryTest, BackWithNothingSelectedReturnsToTheLastSelection) {
            Select({1, 2, 0});
            EXPECT_EQ(Back(), 2u);
            EXPECT_EQ(Back(), 1u);
            Select({0});
            EXPECT_EQ(Back(), 1u);
            Select({0});
            EXPECT_EQ(Forward(), 2u);
        }

        TEST_F(SelectionHistoryTest, DeadNodesAreSkippedAndDropped) {
            Select({1, 2, 3, 4});
            Kill(3);
            Kill(2);
            EXPECT_EQ(Back(), 1u);
            _dead.clear();
            // They are gone from the history, not only passed over
            EXPECT_EQ(Forward(), 4u);
        }

        TEST_F(SelectionHistoryTest, ForwardSkipsADeadNode) {
            Select({1, 2, 3});
            Back();
            Back();
            Kill(2);
            EXPECT_EQ(Forward(), 3u);
            EXPECT_EQ(Back(), 1u);
        }

        TEST_F(SelectionHistoryTest, ADroppedEntryNeverMakesAStepThatSelectsTheSameNode) {
            Select({1, 2, 1, 2, 1});
            Kill(2);
            EXPECT_EQ(Back(), 0u);
            EXPECT_EQ(Forward(), 0u);
        }

        TEST_F(SelectionHistoryTest, BackWithNothingSelectedPassesADeadLastSelection) {
            Select({1, 2, 0});
            Kill(2);
            EXPECT_EQ(Back(), 1u);
            EXPECT_EQ(Forward(), 0u);
        }

        TEST_F(SelectionHistoryTest, ASelectionNotToldYetIsRecordedByTheStep) {
            Select({1, 2});
            _selected = 3;
            EXPECT_EQ(Back(), 2u);
            EXPECT_EQ(Forward(), 3u);
        }

        TEST_F(SelectionHistoryTest, KeepsTheLastSelectionsOnly) {
            constexpr uint64_t kSelections = SelectionHistory::kCapacity + 5;
            for (uint64_t id = 1; id <= kSelections; ++id) {
                Select({id});
            }
            size_t steps = 0;
            uint64_t oldest = 0;
            while (const uint64_t id = Back()) {
                oldest = id;
                ++steps;
            }
            EXPECT_EQ(steps, SelectionHistory::kCapacity - 1);
            EXPECT_EQ(oldest, kSelections - SelectionHistory::kCapacity + 1);
        }

        TEST_F(SelectionHistoryTest, ClearForgetsEverything) {
            Select({1, 2, 3});
            Back();
            _history.Clear();
            _selected = 0;
            EXPECT_EQ(Back(), 0u);
            EXPECT_EQ(Forward(), 0u);
        }

        // The editor asks ImGui::Shortcut for every chord whatever has the keyboard; Option+arrows move by word in a text field, so ImGui must keep them there
        class SelectionChordTest : public ::testing::TestWithParam<bool> {
        protected:
            void SetUp() override {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = ImVec2(800.0f, 600.0f);
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                io.ConfigMacOSXBehaviors = GetParam();
            }

            void TearDown() override {
                ImGui::DestroyContext();
            }

            /// One frame: the chord is asked for in a window of its own, as the editor's dispatch does, then a second window draws a text field
            void Frame(ImGuiKeyChord chord, bool focusText) {
                ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
                ImGui::NewFrame();
                ImGui::Begin("Dispatch");
                _fired |= ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal);
                ImGui::End();
                ImGui::Begin("Panel");
                if (focusText) {
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::InputText("##text", _text, sizeof(_text));
                ImGui::End();
                ImGui::Render();
            }

            /// Whether pressing the default chord of @p command reaches the editor, with the text field holding the keyboard or not
            bool Fires(PoolString command, bool inText) {
                const EditorCommand* const row = FindEditorCommand(command);
                if (row == nullptr || !row->defaultChord) {
                    ADD_FAILURE() << command.CStr() << " has no default chord";
                    return false;
                }
                const ImGuiKeyChord chord = row->defaultChord->ToImGui();
                for (int i = 0; i < 3; ++i) {
                    Frame(chord, inText && i == 0);
                }
                EXPECT_EQ(ImGui::GetIO().WantTextInput, inText);
                _fired = false;
                const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
                for (const bool down : {true, false}) {
                    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, down);
                    ImGui::GetIO().AddKeyEvent(key, down);
                    for (int i = 0; i < 3; ++i) {
                        Frame(chord, false);
                    }
                }
                return _fired;
            }

            char _text[32] = "two words";
            bool _fired = false;
        };

        TEST_P(SelectionChordTest, ReachesTheEditorOnlyWhileNoTextFieldHasTheKeyboard) {
            for (const PoolString command : {PoolString(EditorCommandIds::kSceneEditorSelectionBack), PoolString(EditorCommandIds::kSceneEditorSelectionForward)}) {
                EXPECT_TRUE(Fires(command, false)) << command.CStr();
                EXPECT_FALSE(Fires(command, true)) << command.CStr();
                ImGui::DestroyContext();
                SetUp();
            }
        }

        INSTANTIATE_TEST_SUITE_P(Platforms, SelectionChordTest, ::testing::Bool(), [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "MacBehaviour" : "OtherBehaviour"; });

    }  // namespace

}  // namespace BECore::Tests
