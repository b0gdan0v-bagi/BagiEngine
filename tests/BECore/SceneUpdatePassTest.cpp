#include "SceneUpdatePassTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Time/Time.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Events/SceneEvents.h>
#include <Generated/SceneUpdatePassTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Fixed step and frame time in one: a pass advances the clock by it, so each pass runs exactly one fixed step
        constexpr double kStep = 1.0 / 64.0;
        constexpr size_t kIdleProbes = 1000;
        constexpr size_t kSteadyProbes = 100;
        constexpr int kSteadyFrames = 10;

        // The log the probes write into; null while a test does not read it (a steady frame must not allocate for it)
        Vector<String>* g_log = nullptr;

        void LogCall(const IComponent& component, const char* suffix) {
            if (g_log != nullptr) {
                g_log->push_back(Format("{}{}", component.GetNode().GetName().CStr(), suffix));
            }
        }

        // Main is the active scene (the first), Idle is loaded and inactive
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes/></scene>
  <scene type="Scene" name="Idle"><nodes/></scene>
</scenes></SceneManager>)";

        constexpr const char* kEarlyScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Early"><components><component type="EarlyUpdateProbe"/></components></node>
  </nodes></scene>
  <scene type="Scene" name="Idle"><nodes>
    <node name="Early"><components><component type="EarlyUpdateProbe"/></components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr const char* kProbeNodeFile = R"(<node name="Box"><components><component type="UpdateProbe"/></components></node>)";

    }  // namespace

    void UpdateProbe::OnAttached() {
        if (frame) {
            SubscribeInScene<SceneEvents::SceneUpdateEvent, &UpdateProbe::OnUpdate>(this);
        }
        if (fixed) {
            SubscribeInScene<SceneEvents::SceneFixedUpdateEvent, &UpdateProbe::OnFixedUpdate>(this);
        }
    }

    void UpdateProbe::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        ++updates;
        lastDeltaTime = event.deltaTime;
        lastEvent = &event;
        LogCall(*this, "");
        if (action) {
            action(*this);
        }
    }

    void UpdateProbe::OnFixedUpdate(const SceneEvents::SceneFixedUpdateEvent& event) {
        ++fixedSteps;
        lastFixedDeltaTime = event.deltaTime;
        lastFixedEvent = &event;
        LogCall(*this, "/fixed");
    }

    void EarlyUpdateProbe::Initialize() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &EarlyUpdateProbe::OnUpdate>(this);
    }

    void EarlyUpdateProbe::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        ++updates;
    }

    /// Scene::Update() over components that joined the update lists, on a SceneManager with an active and an inactive scene
    class SceneUpdatePassTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetTime().Reset();
            CoreManager::GetTime().SetFixedDeltaTime(kStep);
            g_log = &_log;
            ASSERT_NO_FATAL_FAILURE(Load(kScenesXml));
        }

        void TearDown() override {
            g_log = nullptr;
            _manager.Reset();
            CoreManager::GetTime().SetFixedDeltaTime(Time::kDefaultFixedDeltaTime);
            CoreManager::GetTime().Reset();
        }

        void Load(const char* xml) {
            _manager.Reset();
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml));
            _manager = BECore::New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        Scene& Main() const {
            return Require(_manager->GetSceneByName("Main"_intern));
        }

        Scene& Idle() const {
            return Require(_manager->GetSceneByName("Idle"_intern));
        }

        /// A node @p name under @p parent (the root when null) with an UpdateProbe that joins the frame list
        IntrusivePtr<SceneNode> AddProbe(const char* name, Scene& scene, SceneNode* parent = nullptr) {
            return AddProbe(name, scene, parent, BECore::New<UpdateProbe>());
        }

        IntrusivePtr<SceneNode> AddProbe(const char* name, Scene& scene, SceneNode* parent, IntrusivePtr<UpdateProbe> probe) {
            IntrusivePtr<SceneNode> node = scene.CreateNode(PoolString::Intern(name), parent);
            node->AddComponent(probe);
            return node;
        }

        static UpdateProbe& ProbeOf(const IntrusivePtr<SceneNode>& node) {
            return Require(node->GetComponent<UpdateProbe>());
        }

        /// One frame of one fixed step; the calls it made, in order
        Vector<String> Pass(double frameTime = kStep) {
            _log.clear();
            CoreManager::GetTime().Advance(frameTime);
            _manager->UpdateAll();
            return _log;
        }

        size_t Frames() const {
            return Main().GetUpdaterCount(SceneUpdateKind::Frame);
        }

        IntrusivePtr<SceneManager> _manager;
        Vector<String> _log;
    };

    TEST_F(SceneUpdatePassTest, EachSubscriptionJoinsTheListOfItsEvent) {
        auto both = BECore::New<UpdateProbe>();
        both->fixed = true;
        AddProbe("Both", Main(), nullptr, both);
        AddProbe("Frame", Main());

        EXPECT_EQ(Main().GetUpdaterCount(SceneUpdateKind::Frame), 2u);
        EXPECT_EQ(Main().GetUpdaterCount(SceneUpdateKind::Fixed), 1u);
        EXPECT_EQ(Idle().GetUpdaterCount(SceneUpdateKind::Frame), 0u);
    }

    TEST_F(SceneUpdatePassTest, TheFixedStepsRunBeforeTheFrameAndEveryHandlerGetsTheEventOfThePass) {
        auto first = BECore::New<UpdateProbe>();
        first->fixed = true;
        auto second = BECore::New<UpdateProbe>();
        second->fixed = true;
        AddProbe("First", Main(), nullptr, first);
        AddProbe("Second", Main(), nullptr, second);

        const Vector<String> calls = Pass(kStep * 2);

        EXPECT_EQ(calls, (Vector<String>{"Second/fixed", "First/fixed", "Second/fixed", "First/fixed", "Second", "First"}));
        EXPECT_EQ(first->lastDeltaTime, static_cast<float>(kStep * 2));
        EXPECT_EQ(first->lastFixedDeltaTime, static_cast<float>(kStep));
        EXPECT_EQ(first->lastEvent, second->lastEvent);
        EXPECT_EQ(first->lastFixedEvent, second->lastFixedEvent);
        EXPECT_NE(first->lastEvent, first->lastFixedEvent);
    }

    TEST_F(SceneUpdatePassTest, AComponentThatSubscribesBeforeItHasASceneUpdatesThroughTheChannelWhileItsSceneIsActive) {
        ASSERT_NO_FATAL_FAILURE(Load(kEarlyScenesXml));
        EarlyUpdateProbe& inMain = Require(Main().GetRootNode().GetChildren().front()->GetComponent<EarlyUpdateProbe>());
        EarlyUpdateProbe& inIdle = Require(Idle().GetRootNode().GetChildren().front()->GetComponent<EarlyUpdateProbe>());

        EXPECT_EQ(Main().GetUpdaterCount(SceneUpdateKind::Frame), 0u);
        Pass();

        EXPECT_EQ(inMain.updates, 1);
        EXPECT_EQ(inIdle.updates, 0);
    }

    TEST_F(SceneUpdatePassTest, TheLastComponentAttachedRunsFirstWhateverTheTreeOrder) {
        auto first = AddProbe("First", Main());
        auto second = AddProbe("Second", Main());
        auto third = AddProbe("Third", Main());

        EXPECT_EQ(Pass(), (Vector<String>{"Third", "Second", "First"}));

        // The tree stays First, Second, Third: a component attached to the first node later runs before the others
        first->RemoveComponent("UpdateProbe"_intern);
        first->AddComponent(BECore::New<UpdateProbe>());
        EXPECT_EQ(Pass(), (Vector<String>{"First", "Third", "Second"}));
    }

    TEST_F(SceneUpdatePassTest, ARemovalInTheMiddleKeepsTheOrderOfTheOthers) {
        auto first = AddProbe("First", Main());
        auto second = AddProbe("Second", Main());
        auto third = AddProbe("Third", Main());
        auto fourth = AddProbe("Fourth", Main());

        Main().GetRootNode().RemoveChild(second.Get());

        EXPECT_EQ(Pass(), (Vector<String>{"Fourth", "Third", "First"}));
        EXPECT_EQ(Frames(), 3u);
    }

    TEST_F(SceneUpdatePassTest, MovingANodeChangesNeitherTheRegistrationNorTheOrder) {
        auto first = AddProbe("First", Main());
        auto second = AddProbe("Second", Main());
        auto third = AddProbe("Third", Main());

        ASSERT_TRUE(Main().MoveNode(*first, third.Get()));
        ASSERT_TRUE(Main().MoveNode(*third, nullptr, 0));

        EXPECT_EQ(Frames(), 3u);
        EXPECT_EQ(Pass(), (Vector<String>{"Third", "Second", "First"}));
    }

    TEST_F(SceneUpdatePassTest, ADuplicateAndAnInstanceRegisterTheirOwnComponent) {
        auto original = AddProbe("Original", Main());

        const IntrusivePtr<SceneNode> copy = Main().DuplicateNode(*original);
        ASSERT_TRUE(copy);
        const auto loader = [](StringView, pugi::xml_document& out, String&) { return static_cast<bool>(out.load_string(kProbeNodeFile)); };
        pugi::xml_document reference;
        reference.append_child("node").append_attribute("ref").set_value("nodes/Box");
        const IntrusivePtr<SceneNode> instance = Main().InstantiateNode(reference.document_element(), loader);
        ASSERT_TRUE(instance);

        EXPECT_EQ(Frames(), 3u);
        const Vector<String> calls = Pass();
        ASSERT_EQ(calls.size(), 3u);
        EXPECT_EQ(calls.front(), "Box");
        EXPECT_EQ(calls.back(), "Original");
    }

    TEST_F(SceneUpdatePassTest, ANodeAddedByAHandlerIsFirstCalledInTheNextPass) {
        auto adder = AddProbe("Adder", Main());
        bool added = false;
        ProbeOf(adder).action = [&](UpdateProbe&) {
            if (!added) {
                added = true;
                AddProbe("Added", Main());
            }
        };

        EXPECT_EQ(Pass(), (Vector<String>{"Adder"}));
        EXPECT_EQ(Frames(), 2u);
        EXPECT_EQ(Pass(), (Vector<String>{"Added", "Adder"}));
    }

    TEST_F(SceneUpdatePassTest, ARemovedNodeOfALaterEntryIsNotCalledInThePass) {
        auto victim = AddProbe("Victim", Main());
        auto killer = AddProbe("Killer", Main());
        ProbeOf(killer).action = [&](UpdateProbe&) { Main().GetRootNode().RemoveChild(victim.Get()); };

        EXPECT_EQ(Pass(), (Vector<String>{"Killer"}));
        EXPECT_EQ(Frames(), 1u);
        EXPECT_EQ(ProbeOf(victim).updates, 0);
    }

    TEST_F(SceneUpdatePassTest, ANodeThatRemovesItselfFinishesThePassAndIsNotCalledAgain) {
        auto other = AddProbe("Other", Main());
        auto self = AddProbe("Self", Main());
        ProbeOf(self).action = [&](UpdateProbe&) { Main().GetRootNode().RemoveChild(self.Get()); };

        EXPECT_EQ(Pass(), (Vector<String>{"Self", "Other"}));
        EXPECT_EQ(Pass(), (Vector<String>{"Other"}));
        EXPECT_EQ(Frames(), 1u);
    }

    TEST_F(SceneUpdatePassTest, ARemovedComponentOfALaterEntryIsNotCalledInThePass) {
        auto victim = AddProbe("Victim", Main());
        auto killer = AddProbe("Killer", Main());
        ProbeOf(killer).action = [&](UpdateProbe&) { victim->RemoveComponent("UpdateProbe"_intern); };

        EXPECT_EQ(Pass(), (Vector<String>{"Killer"}));
        EXPECT_EQ(Frames(), 1u);
    }

    TEST_F(SceneUpdatePassTest, AComponentOfTheSameTypeReplacesTheOldOneForGood) {
        auto victim = AddProbe("Victim", Main());
        IntrusivePtr<UpdateProbe> replaced = victim->GetComponent<UpdateProbe>();
        auto replacement = BECore::New<UpdateProbe>();
        auto killer = AddProbe("Killer", Main());
        ProbeOf(killer).action = [&](UpdateProbe&) {
            if (replacement) {
                victim->AddComponent(eastl::move(replacement));
            }
        };

        EXPECT_EQ(Pass(), (Vector<String>{"Killer"}));
        EXPECT_EQ(Pass(), (Vector<String>{"Victim", "Killer"}));
        EXPECT_EQ(replaced->updates, 0);
        EXPECT_EQ(Require(victim->GetComponent<UpdateProbe>()).updates, 1);
        EXPECT_EQ(Frames(), 2u);
    }

    TEST_F(SceneUpdatePassTest, ASceneSwitchFromAHandlerDoesNotCutThePassAndTheNewSceneUpdatesNextFrame) {
        auto idle = AddProbe("IdleProbe", Idle());
        auto tail = AddProbe("Tail", Main());
        auto switcher = AddProbe("Switcher", Main());
        ProbeOf(switcher).action = [&](UpdateProbe&) { _manager->SetActiveScene("Idle"_intern); };

        EXPECT_EQ(Pass(), (Vector<String>{"Switcher", "Tail"}));
        EXPECT_EQ(Pass(), (Vector<String>{"IdleProbe"}));
    }

    TEST_F(SceneUpdatePassTest, ARemovedComponentStaysSilentAndItsReleaseIsSafe) {
        auto node = AddProbe("Node", Main());
        IntrusivePtr<UpdateProbe> held = node->GetComponent<UpdateProbe>();

        node->RemoveComponent("UpdateProbe"_intern);

        EXPECT_EQ(Frames(), 0u);
        EXPECT_TRUE(Pass().empty());
        EXPECT_EQ(held->updates, 0);
        held.Reset();
        EXPECT_TRUE(Pass().empty());
    }

    TEST_F(SceneUpdatePassTest, ARemovedSubtreeLeavesAtEveryDepth) {
        auto parent = AddProbe("Parent", Main());
        auto child = AddProbe("Child", Main(), parent.Get());
        auto grandchild = AddProbe("Grandchild", Main(), child.Get());
        AddProbe("Stays", Main());
        ASSERT_EQ(Frames(), 4u);

        Main().GetRootNode().RemoveChild(parent.Get());

        EXPECT_EQ(Frames(), 1u);
        EXPECT_EQ(Pass(), (Vector<String>{"Stays"}));
        EXPECT_EQ(ProbeOf(grandchild).updates, 0);
    }

    TEST_F(SceneUpdatePassTest, TheLastReferenceOfARemovedNodeCanGoWithoutATrace) {
        auto node = AddProbe("Node", Main());
        Main().GetRootNode().RemoveChild(node.Get());

        node.Reset();

        EXPECT_EQ(Frames(), 0u);
        EXPECT_TRUE(Pass().empty());
    }

    TEST_F(SceneUpdatePassTest, TheSceneMayDieBeforeTheNodeAndTheComponentItDroppedOrStillHeldByTheCaller) {
        auto attached = AddProbe("Attached", Main());
        auto detached = AddProbe("Detached", Main());
        auto stripped = AddProbe("Stripped", Main());
        IntrusivePtr<UpdateProbe> component = stripped->GetComponent<UpdateProbe>();
        Main().GetRootNode().RemoveChild(detached.Get());
        stripped->RemoveComponent("UpdateProbe"_intern);

        _manager.Reset();

        component.Reset();
        detached.Reset();
        stripped.Reset();
        attached.Reset();
        SUCCEED();
    }

    TEST_F(SceneUpdatePassTest, AnInactiveSceneCallsNothingAndAllocatesNothingUntilItIsActive) {
        g_log = nullptr;
        Vector<IntrusivePtr<UpdateProbe>> probes;
        Vector<IntrusivePtr<SceneNode>> nodes;
        for (size_t index = 0; index < kIdleProbes; ++index) {
            auto probe = BECore::New<UpdateProbe>();
            probe->fixed = true;
            probes.push_back(probe);
            nodes.push_back(AddProbe("Idle", Idle(), nullptr, probe));
        }
        ASSERT_EQ(Idle().GetUpdaterCount(SceneUpdateKind::Frame), kIdleProbes);
        Pass();

        ExpectNoAllocations([&] {
            for (int pass = 0; pass < 3; ++pass) {
                CoreManager::GetTime().Advance(kStep);
                _manager->UpdateAll();
                Idle().Update();
            }
        });

        for (const auto& probe : probes) {
            ASSERT_EQ(probe->updates, 0);
            ASSERT_EQ(probe->fixedSteps, 0);
        }

        _manager->SetActiveScene("Idle"_intern);
        Pass();

        for (const auto& probe : probes) {
            ASSERT_EQ(probe->updates, 1);
            ASSERT_EQ(probe->fixedSteps, 1);
        }
    }

    TEST_F(SceneUpdatePassTest, ASteadyFrameAllocatesNothing) {
        g_log = nullptr;
        Vector<IntrusivePtr<SceneNode>> nodes;
        for (size_t index = 0; index < kSteadyProbes; ++index) {
            auto probe = BECore::New<UpdateProbe>();
            probe->fixed = true;
            nodes.push_back(AddProbe("Steady", Main(), nullptr, probe));
        }
        Pass();

        ExpectNoAllocations([&] {
            for (int frame = 0; frame < kSteadyFrames; ++frame) {
                CoreManager::GetTime().Advance(kStep);
                _manager->UpdateAll();
            }
        });

        EXPECT_EQ(ProbeOf(nodes.front()).updates, 1 + kSteadyFrames);
        EXPECT_EQ(ProbeOf(nodes.front()).fixedSteps, 1 + kSteadyFrames);
    }

}  // namespace BECore::Tests
