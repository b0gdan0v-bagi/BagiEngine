#include "ComponentEnabledTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/ClipRectComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeInstance.h>
#include <BECore/Scene/SceneNodePath.h>
#include <BECore/Time/Time.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ComponentEnabledTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void EnabledProbeComponent::OnAttached() {
        ++attached;
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &EnabledProbeComponent::OnUpdate>(this);
        SubscribeInScene<SceneEvents::SceneFixedUpdateEvent, &EnabledProbeComponent::OnFixedUpdate>(this);
        SubscribeInScene<InputEvents::ActionEvent, &EnabledProbeComponent::OnAction>(this);
        Subscribe<InputEvents::ActionEvent, &EnabledProbeComponent::OnPlainAction>(this);
    }

    void EnabledProbeComponent::OnDraw() {
        ++draws;
    }

    void EnabledProbeComponent::OnPropertyWritten(StringView field) {
        written.push_back(String(field));
    }

    void EnabledProbeComponent::OnEnabledChanged() {
        ++enabledChanges;
    }

    void EnabledProbeComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        ++updates;
        if (onUpdate) {
            onUpdate();
        }
    }

    void EnabledProbeComponent::OnFixedUpdate(const SceneEvents::SceneFixedUpdateEvent&) {
        ++fixedSteps;
    }

    void EnabledProbeComponent::OnAction(const InputEvents::ActionEvent&) {
        ++actions;
    }

    void EnabledProbeComponent::OnPlainAction(const InputEvents::ActionEvent&) {
        ++plainActions;
    }

    namespace {

        // One fixed step per pass
        constexpr double kStep = 1.0 / 64.0;

        // Nodes of each thing a draw pass makes of a component: a quad, a sprite, an OnDraw() and clips, one inside another
        constexpr StringView kNodes = R"(
<node name="Quad"><components>
  <component type="TransformComponent" x="0" y="0" width="10" height="10"/>
  <component type="QuadRendererComponent"/>
</components></node>
<node name="Sprite"><components>
  <component type="TransformComponent" x="20" y="0" width="10" height="10"/>
  <component type="SpriteRendererComponent"/>
</components></node>
<node name="Probe"><components><component type="EnabledProbeComponent"/></components></node>
<node name="Outer"><components>
  <component type="TransformComponent" x="100" y="100" width="40" height="40"/>
  <component type="ClipRectComponent"/>
</components><children>
  <node name="Inner"><components>
    <component type="TransformComponent" x="110" y="110" width="50" height="50"/>
    <component type="ClipRectComponent"/>
    <component type="QuadRendererComponent"/>
  </components></node>
</children></node>)";

        /// The left edge and the width of the clip a call was drawn under: enough to tell the two clips of kNodes apart
        Pair<float, float> ClipSpan(const DrawCall& call) {
            const Rect& clip = Require(call.clip);
            return {clip.x, clip.w};
        }

        constexpr StringView kSleepingNodes = R"(
<node name="Asleep"><components>
  <component type="TransformComponent" x="1"/>
  <component type="EnabledProbeComponent" enabled="false"/>
</components></node>
<node name="Awake"><components><component type="QuadRendererComponent"/></components></node>)";

        class ComponentEnabledTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                CoreManager::GetTime().Reset();
                CoreManager::GetTime().SetFixedDeltaTime(kStep);
                _renderer = New<RecordingRenderer>();
                CoreManager::GetRendererManager().SetRenderer(_renderer);
            }

            void TearDown() override {
                _manager.Reset();
                CoreManager::GetRendererManager().SetRenderer({});
                CoreManager::GetTime().SetFixedDeltaTime(Time::kDefaultFixedDeltaTime);
                CoreManager::GetTime().Reset();
            }

            void LoadXml(const String& xml) {
                XmlDeserializer deserializer;
                ASSERT_TRUE(deserializer.LoadFromString(xml.c_str()));
                _manager = New<SceneManager>();
                _manager->Deserialize(deserializer);
                ASSERT_FALSE(deserializer.HasErrors());
                _manager->Initialize();
            }

            void Load(StringView nodes) {
                ASSERT_NO_FATAL_FAILURE(LoadXml(SceneConfigText(nodes)));
            }

            Scene& Main() const {
                return Require(_manager->GetActiveScene());
            }

            template <typename T>
            T& Component(StringView path) const {
                const auto found = FindSceneNodeByPath(Main().GetRootNode(), path);
                return Require(Require(found.has_value() ? *found : nullptr).GetComponent<T>());
            }

            IntrusivePtr<EnabledProbeComponent> AddProbe(const char* name) const {
                IntrusivePtr<EnabledProbeComponent> probe = New<EnabledProbeComponent>();
                Main().CreateNode(PoolString::Intern(name))->AddComponent(probe);
                return probe;
            }

            void Step() const {
                CoreManager::GetTime().Advance(kStep);
                _manager->UpdateAll();
            }

            const Vector<DrawCall>& Draw() const {
                _renderer->ClearCalls();
                _manager->DrawAll();
                return _renderer->calls;
            }

            IntrusivePtr<RecordingRenderer> _renderer;
            IntrusivePtr<SceneManager> _manager;
        };

    }  // namespace

    TEST_F(ComponentEnabledTest, ADisabledComponentSleepsInBothListsAndItsSceneSubscriptionsUntilEnabled) {
        ASSERT_NO_FATAL_FAILURE(Load(""));
        const IntrusivePtr<EnabledProbeComponent> probe = AddProbe("Probe");
        probe->SetEnabled(false);

        Step();
        InputEvents::ActionEvent::Emit("Ding"_intern);

        EXPECT_EQ(probe->updates, 0);
        EXPECT_EQ(probe->fixedSteps, 0);
        EXPECT_EQ(probe->actions, 0);
        EXPECT_EQ(probe->plainActions, 1) << "a plain subscription is not the scene's";

        probe->SetEnabled(true);
        Step();
        InputEvents::ActionEvent::Emit("Ding"_intern);

        EXPECT_EQ(probe->updates, 1);
        EXPECT_EQ(probe->fixedSteps, 1);
        EXPECT_EQ(probe->actions, 1);
    }

    TEST_F(ComponentEnabledTest, TheFlagIsReadAtEachCallOfAPass) {
        ASSERT_NO_FATAL_FAILURE(Load(""));
        const IntrusivePtr<EnabledProbeComponent> later = AddProbe("Later");  // added first, so it runs second
        const IntrusivePtr<EnabledProbeComponent> first = AddProbe("First");
        first->onUpdate = [&later] { later->SetEnabled(false); };

        Step();

        EXPECT_EQ(later->updates, 0);

        first->onUpdate = [&later] { later->SetEnabled(true); };
        Step();

        EXPECT_EQ(later->updates, 1);
    }

    TEST_F(ComponentEnabledTest, TheHookRunsOnARealChangeOnlyAlsoWhenDetached) {
        const IntrusivePtr<EnabledProbeComponent> probe = New<EnabledProbeComponent>();

        probe->SetEnabled(true);
        probe->SetEnabled(false);
        probe->SetEnabled(false);

        EXPECT_EQ(probe->enabledChanges, 1);
        EXPECT_FALSE(probe->IsEnabled());
    }

    TEST_F(ComponentEnabledTest, TheDrawPassLeavesDisabledComponentsOutWithoutARebuild) {
        ASSERT_NO_FATAL_FAILURE(Load(kNodes));
        const uint8_t texel[4] = {255, 255, 255, 255};
        Component<SpriteRendererComponent>("Sprite").SetTexture(_renderer->CreateTexture(texel, 1, 1));
        auto& probe = Component<EnabledProbeComponent>("Probe");
        auto& quad = Component<QuadRendererComponent>("Quad");
        auto& sprite = Component<SpriteRendererComponent>("Sprite");
        auto& outer = Component<ClipRectComponent>("Outer");
        auto& inner = Component<ClipRectComponent>("Outer/Inner");
        const Pair<float, float> both(110.0f, 30.0f);
        const Pair<float, float> innerOnly(110.0f, 50.0f);

        ASSERT_EQ(Draw().size(), 3u);
        EXPECT_EQ(ClipSpan(At(_renderer->calls, 2)), both);
        const uint32_t builds = Main().GetDrawListBuilds();

        quad.SetEnabled(false);
        sprite.SetEnabled(false);
        probe.SetEnabled(false);
        outer.SetEnabled(false);

        ASSERT_EQ(Draw().size(), 1u);
        EXPECT_EQ(ClipSpan(At(_renderer->calls, 0)), innerOnly) << "only the disabled clip is gone";
        EXPECT_EQ(probe.draws, 1);

        inner.SetEnabled(false);

        EXPECT_FALSE(At(Draw(), 0).clip.has_value());

        quad.SetEnabled(true);
        sprite.SetEnabled(true);
        probe.SetEnabled(true);
        outer.SetEnabled(true);
        inner.SetEnabled(true);

        ASSERT_EQ(Draw().size(), 3u);
        EXPECT_EQ(ClipSpan(At(_renderer->calls, 2)), both);
        EXPECT_EQ(probe.draws, 2);
        EXPECT_EQ(Main().GetDrawListBuilds(), builds);
    }

    TEST_F(ComponentEnabledTest, AComponentLoadedDisabledIsAttachedButGetsNoHook) {
        ASSERT_NO_FATAL_FAILURE(Load(kSleepingNodes));
        const auto& probe = Component<EnabledProbeComponent>("Asleep");

        Step();

        EXPECT_FALSE(probe.IsEnabled());
        EXPECT_EQ(probe.attached, 1);
        EXPECT_EQ(probe.enabledChanges, 0);
        EXPECT_EQ(probe.updates, 0);
    }

    TEST_F(ComponentEnabledTest, TheFlagSurvivesXmlWithOneEnabledAttributePerComponent) {
        ASSERT_NO_FATAL_FAILURE(Load(kSleepingNodes));
        XmlSerializer serializer;
        _manager->Serialize(serializer);
        const String saved = serializer.SaveToString();
        pugi::xml_document document;
        ASSERT_TRUE(document.load_string(saved.c_str()));
        size_t components = 0;
        for (const pugi::xpath_node found : document.select_nodes("//components/item")) {
            ++components;
            size_t flags = 0;
            for (const pugi::xml_attribute attribute : found.node().attributes()) {
                flags += StringView(attribute.name()) == "enabled" ? 1 : 0;
            }
            EXPECT_EQ(flags, 1u) << found.node().attribute("type").as_string();
        }
        EXPECT_EQ(components, 3u);

        ASSERT_NO_FATAL_FAILURE(LoadXml(saved));

        EXPECT_FALSE(Component<EnabledProbeComponent>("Asleep").IsEnabled());
        EXPECT_TRUE(Component<QuadRendererComponent>("Awake").IsEnabled());
    }

    TEST_F(ComponentEnabledTest, TheFlagSurvivesBinary) {
        ASSERT_NO_FATAL_FAILURE(Load(kSleepingNodes));
        const auto found = FindSceneNodeByPath(Main().GetRootNode(), "Asleep");
        ASSERT_TRUE(found.has_value());
        BinarySerializer writer;
        (*found)->Serialize(writer);
        const Vector<uint8_t> bytes = writer.GetData();
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));

        const IntrusivePtr<SceneNode> loaded = DeserializeNode(reader, "binary");

        EXPECT_FALSE(Require(loaded->GetComponent<EnabledProbeComponent>()).IsEnabled());
    }

    TEST_F(ComponentEnabledTest, NoComponentClassHasASecondFieldSerializedAsEnabled) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const auto& types = AbstractFactory<IComponent>::GetInstance().GetRegisteredTypes();
        ASSERT_FALSE(types.empty());
        for (const ClassMeta& meta : types) {
            size_t flags = 0;
            for (const TypeInfo* info = registry.Find(meta.typeHash); info != nullptr; info = info->parentHash != 0 ? registry.Find(info->parentHash) : nullptr) {
                for (const FieldDesc& field : info->fields) {
                    flags += field.name == "enabled" ? 1 : 0;
                }
            }
            EXPECT_EQ(flags, 1u) << meta.typeName.data();
        }
    }

}  // namespace BECore::Tests
