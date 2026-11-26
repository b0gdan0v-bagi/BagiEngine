// The touch record of the control API (ControlService::Touches()): which call a report belongs to, what the journal keeps of it, and the node ids it names.

#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlTouches.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        class ControlTouchesTest : public ::testing::Test {
        protected:
            void SetUp() override {
                RegisterControlBuiltins(service);
                // `touch {node}` reports the node; `later {node}` does it from the `done` of a deferred call
                service.RegisterHandler("touch", [this](ControlCall& call) {
                    service.ReportTouch(NodeParam(call), "Comp", "field");
                    call.ReplyEmpty();
                });
                service.RegisterHandler("later", [this](ControlCall& call) {
                    call.DeferFrames(1, [this](ControlCall& done) {
                        service.ReportTouch(NodeParam(done));
                        done.ReplyEmpty();
                    });
                });
            }

            static uint64_t NodeParam(const ControlCall& call) {
                return static_cast<uint64_t>(GetParamInt(call.Params(), "node").value_or(0));
            }

            void Send(StringView method, StringView params = {}, const IntrusivePtr<ControlClient>& client = {}) {
                service.Post(MakeRequestText(1, method, params), [](const String&) {}, client);
                service.Drain();
            }

            const ControlTouch& Touch(size_t index) const {
                return Require(index < service.Touches().Size() ? &service.Touches().At(index) : nullptr);
            }

            const ControlActivityEntry& Entry(size_t index) const {
                return Require(index < service.Activity().Size() ? &service.Activity().At(index) : nullptr);
            }

            ControlService service;
            IntrusivePtr<ControlClient> tool = service.OpenLocalClient("tool");
        };

    }  // namespace

    TEST_F(ControlTouchesTest, AReportKeepsWhoTouchedWhatAndTheJournalKeepsTheFirstNodeOfTheCall) {
        service.RegisterHandler("two", [this](ControlCall& call) {
            service.ReportTouch(7, "Comp", "field");
            service.ReportTouch(8);
            call.ReplyEmpty();
        });
        const uint64_t before = service.Touches().GetVersion();

        Send("two", {}, tool);

        EXPECT_NE(service.Touches().GetVersion(), before);
        ASSERT_EQ(service.Touches().Size(), 2u);
        EXPECT_EQ(Touch(0).nodeId, 7u);
        EXPECT_EQ(Touch(0).component, "Comp");
        EXPECT_EQ(Touch(0).field, "field");
        EXPECT_EQ(Touch(0).client.Get(), tool.Get());
        EXPECT_EQ(Touch(1).nodeId, 8u);
        EXPECT_EQ(Touch(1).component, "");
        EXPECT_EQ(Touch(1).field, "");
        EXPECT_LT(Touch(0).sequence, Touch(1).sequence);
        EXPECT_EQ(Entry(0).node, 7u);
    }

    TEST_F(ControlTouchesTest, AFullRecordDropsItsOldestReports) {
        constexpr size_t kExtra = 3;
        for (size_t i = 0; i < ControlTouches::kCapacity + kExtra; ++i) {
            Send("touch", Format("{{\"node\":{}}}", i + 1), tool);
        }

        ASSERT_EQ(service.Touches().Size(), ControlTouches::kCapacity);
        for (size_t i = 0; i < ControlTouches::kCapacity; ++i) {
            ASSERT_EQ(Touch(i).nodeId, i + kExtra + 1) << i;
            ASSERT_EQ(Touch(i).sequence, i + kExtra + 1) << i;
        }
        EXPECT_EQ(service.Touches().GetNextSequence(), ControlTouches::kCapacity + kExtra + 1);
    }

    TEST_F(ControlTouchesTest, NothingIsRecordedWithNoClientNoCallOrNoNode) {
        Send("touch", R"({"node":5})");
        service.ReportTouch(5);
        service.ReportTouch(nullptr, 5);
        Send("touch", R"({"node":0})", tool);

        EXPECT_EQ(service.Touches().Size(), 0u);
        EXPECT_EQ(service.GetCurrentClient(), nullptr);
        ASSERT_EQ(service.Activity().Size(), 2u);
        EXPECT_EQ(Entry(0).node, 0u);
        EXPECT_EQ(Entry(1).node, 0u);
    }

    TEST_F(ControlTouchesTest, ANestedCallReportsUnderItsClientAndIntoItsOwnEntry) {
        Send("batch", R"({"calls":[{"method":"touch","params":{"node":3}},{"method":"touch","params":{"node":4}}]})", tool);

        ASSERT_EQ(service.Touches().Size(), 2u);
        EXPECT_EQ(Touch(0).client.Get(), tool.Get());
        EXPECT_EQ(Touch(1).client.Get(), tool.Get());
        ASSERT_EQ(service.Activity().Size(), 2u);
        EXPECT_EQ(Entry(0).node, 3u);
        EXPECT_EQ(Entry(1).node, 4u);
    }

    TEST_F(ControlTouchesTest, TheOuterCallIsTheRunningOneAgainAfterANestedCall) {
        service.RegisterHandler("outer", [this](ControlCall& call) {
            const auto answer = service.RunNested(call, "touch", call.Params(), [](ControlCall&, ControlAnswer) {});
            EXPECT_TRUE(answer.has_value());
            service.ReportTouch(9);
            call.ReplyEmpty();
        });

        Send("outer", R"({"node":3})", tool);

        ASSERT_EQ(service.Activity().Size(), 2u);
        EXPECT_EQ(Entry(0).method, "touch");
        EXPECT_EQ(Entry(0).node, 3u);
        EXPECT_EQ(Entry(1).method, "outer");
        EXPECT_EQ(Entry(1).node, 9u);
    }

    TEST_F(ControlTouchesTest, ADeferredCallReportsFromItsDone) {
        Send("later", R"({"node":11})", tool);
        EXPECT_EQ(service.GetCurrentClient(), nullptr);  // the call waits: it does not run
        service.EndFrame();

        ASSERT_EQ(service.Touches().Size(), 1u);
        EXPECT_EQ(Touch(0).nodeId, 11u);
        EXPECT_EQ(Touch(0).client.Get(), tool.Get());
        ASSERT_EQ(service.Activity().Size(), 1u);
        EXPECT_EQ(Entry(0).node, 11u);
        EXPECT_EQ(service.GetCurrentClient(), nullptr);
    }

    TEST_F(ControlTouchesTest, ADeferredNestedCallReportsIntoItsOwnEntry) {
        service.SetManualClock(true);

        Send("batch", R"({"calls":[{"method":"later","params":{"node":12}},{"method":"touch","params":{"node":13}}]})", tool);
        service.EndFrame();

        ASSERT_EQ(service.Touches().Size(), 2u);
        EXPECT_EQ(Touch(0).nodeId, 12u);
        EXPECT_EQ(Touch(0).client.Get(), tool.Get());
        ASSERT_EQ(service.Activity().Size(), 2u);
        EXPECT_EQ(Entry(0).node, 12u);
        EXPECT_EQ(Entry(1).node, 13u);
    }

    TEST_F(ControlTouchesTest, AKeptClientReportsAfterItsCallEnded) {
        IntrusivePtr<ControlClient> kept;
        service.RegisterHandler("keep", [this, &kept](ControlCall& call) {
            kept = service.GetCurrentClient();
            call.ReplyEmpty();
        });
        Send("keep", {}, tool);

        service.ReportTouch(kept.Get(), 21, "Comp", "field");

        ASSERT_EQ(service.Touches().Size(), 1u);
        EXPECT_EQ(Touch(0).nodeId, 21u);
        EXPECT_EQ(Touch(0).client.Get(), tool.Get());
        ASSERT_EQ(service.Activity().Size(), 1u);
        EXPECT_EQ(Entry(0).node, 0u);  // its entry was written when the call was answered
    }

    TEST_F(ControlTouchesTest, AWarmRecordAllocatesNothing) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        // Longer than a string keeps inside itself, so every slot has to have grown a buffer
        constexpr StringView kComponent = "AComponentWithANameLongerThanTheInlineBuffer";
        constexpr StringView kField = "aFieldWithANameLongerThanTheInlineBuffer";
        for (size_t i = 0; i < ControlTouches::kCapacity; ++i) {
            service.ReportTouch(tool.Get(), 1, kComponent, kField);
        }

        const AllocCount counted = CountAllocations([&] { service.ReportTouch(tool.Get(), 2, kComponent, kField); });

        EXPECT_EQ(counted.count, 0u);
    }

    /// The node ids the reports name, on a scene loaded from a string
    class ControlTouchesSceneTest : public ControlTouchesTest {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ControlTouchesTest::SetUp();
            XmlDeserializer deserializer;
            const String xml = SceneConfigText(R"(<node name="A"><children><node name="B"/></children></node>)");
            ASSERT_TRUE(deserializer.LoadFromString(xml.c_str()));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _scene = _manager->GetSceneByName("Main"_intern);
            ASSERT_NE(_scene, nullptr);
        }

        SceneNode& Node(const char* name) {
            return Require(FindNodeBelow(_scene->GetRootNode(), name));
        }

        IntrusivePtr<SceneManager> _manager;
        Scene* _scene = nullptr;
    };

    TEST_F(ControlTouchesSceneTest, ADuplicateHasIdsOfItsOwn) {
        const IntrusivePtr<SceneNode> copy = _scene->DuplicateNode(Node("A"));
        ASSERT_TRUE(copy);
        ASSERT_EQ(copy->GetChildren().size(), 1u);

        const uint64_t ids[] = {_scene->GetRootNode().GetRuntimeId(), Node("A").GetRuntimeId(), Node("B").GetRuntimeId(), copy->GetRuntimeId(), copy->GetChildren().front()->GetRuntimeId()};
        for (size_t i = 0; i < std::size(ids); ++i) {
            EXPECT_NE(ids[i], 0u) << i;
            for (size_t other = i + 1; other < std::size(ids); ++other) {
                EXPECT_NE(ids[i], ids[other]) << i << " and " << other;
            }
        }
    }

    TEST_F(ControlTouchesSceneTest, ANodeIsFoundByItsIdUntilItIsRemoved) {
        SceneNode& root = _scene->GetRootNode();
        const uint64_t a = Node("A").GetRuntimeId();
        const uint64_t b = Node("B").GetRuntimeId();
        EXPECT_EQ(FindNodeByRuntimeId(root, b), &Node("B"));
        EXPECT_EQ(FindNodeByRuntimeId(root, root.GetRuntimeId()), &root);

        root.RemoveChild(&Node("A"));

        EXPECT_EQ(FindNodeByRuntimeId(root, a), nullptr);
        EXPECT_EQ(FindNodeByRuntimeId(root, b), nullptr);
    }

    TEST_F(ControlTouchesSceneTest, ReportNodeTouchedNamesTheNodeByItsId) {
        service.RegisterHandler("poke", [this](ControlCall& call) {
            ReportNodeTouched(service, Node("B"), "Comp");
            call.ReplyEmpty();
        });

        Send("poke", {}, tool);

        ASSERT_EQ(service.Touches().Size(), 1u);
        EXPECT_EQ(Touch(0).nodeId, Node("B").GetRuntimeId());
        EXPECT_EQ(Touch(0).component, "Comp");
    }

}  // namespace BECore::Tests
