// The walk of a remote component through a property visitor, against the local component the data came from, and the writes it makes.

#include "RemoteFieldsTest.h"

#include <BECore/Control/ControlProperty.h>
#include <BECore/Control/ReflectionControl.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodePath.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/NullPropertyVisitor.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Generated/RemoteFieldsTest.gen.hpp>
#include <Widgets/RemoteEditor/RemoteComponent.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr StringView kNodes = R"(
<node name="Thing"><components>
  <component type="TransformComponent" x="1.5" y="-2" width="3" height="4" rotation="30"/>
  <component type="QuadRendererComponent" blend="Add"><color r="10" g="20" b="30" a="255"/></component>
  <component type="SpriteRendererComponent" flipX="true"><srcRect x="1" y="2" w="3" h="4"/></component>
  <component type="TextComponent" text="Hello" fontWeight="700"/>
  <component type="UICanvasComponent"/>
</components></node>
<node name="Hud"><components>
  <component type="UICanvasComponent"><elements>
    <element type="UIPanel" name="Plate" width="100" height="40" color="1,2,3,255"><children><element type="UIElement" name="Leaf"/></children></element>
  </elements></component>
</components></node>
<node name="Probe"><components><component type="RemoteProbeComponent"/></components></node>)";

        /// Writes down what it is shown, one line per call, and, when asked to, changes every value to another valid one
        class LogVisitor final : public IPropertyVisitor {
        public:
            /// The change every Visit makes; a name in `kept` is left alone
            bool edits = false;
            Vector<StringView> kept;
            Vector<String> log;

            bool Visit(StringView name, bool& value) override {
                Add("bool", name, value);
                return Edit(name, value, !value);
            }
            bool Visit(StringView name, int8_t& value) override {
                return Number("int8", name, value);
            }
            bool Visit(StringView name, uint8_t& value) override {
                return Number("uint8", name, value);
            }
            bool Visit(StringView name, int16_t& value) override {
                return Number("int16", name, value);
            }
            bool Visit(StringView name, uint16_t& value) override {
                return Number("uint16", name, value);
            }
            bool Visit(StringView name, int32_t& value) override {
                return Number("int32", name, value);
            }
            bool Visit(StringView name, uint32_t& value) override {
                return Number("uint32", name, value);
            }
            bool Visit(StringView name, int64_t& value) override {
                return Number("int64", name, value);
            }
            bool Visit(StringView name, uint64_t& value) override {
                return Number("uint64", name, value);
            }
            bool Visit(StringView name, float& value) override {
                return Number("float", name, value);
            }
            bool Visit(StringView name, double& value) override {
                return Number("double", name, value);
            }
            // A remote text is a String whatever the local field is, so both are one line
            bool Visit(StringView name, PoolString& value) override {
                Add("text", name, value.ToStringView());
                return false;
            }
            bool Visit(StringView name, String& value) override {
                Add("text", name, StringView(value));
                // A text the pool holds, so that a name field takes it
                return Edit(name, value, String("Main"));
            }
            bool VisitEnum(StringView name, const StringView* names, size_t count, size_t& selected) override {
                Add("enum", name, names[selected]);
                return Edit(name, selected, (selected + 1) % count);
            }
            bool BeginCompound(StringView name) override {
                Add("compound", name, "");
                return true;
            }
            void EndCompound() override {
                log.push_back("end");
            }
            bool BeginList(StringView name, const ListInfo& info) override {
                Add("list", name, info.size);
                _canChangeList = _canChangeList || info.canAdd || info.canRemove;
                return true;
            }
            void EndListElement(size_t /*index*/, ListRequest& request) override {
                request.action = ListAction::Remove;
            }
            void EndList(ListRequest& /*request*/) override {
                log.push_back("end");
            }
            bool VisitPresence(StringView name, bool& present) override {
                Add("presence", name, present);
                return Edit(name, present, !present);
            }
            bool VisitAlternative(StringView name, size_t /*count*/, size_t& index) override {
                Add("alternative", name, index);
                return Edit(name, index, size_t{0});
            }
            void BeginField(const FieldAttributes& attributes) override {
                _fields.push_back(attributes);
            }
            void EndField() override {
                _fields.pop_back();
            }

            [[nodiscard]] bool SawAChangeableList() const {
                return _canChangeList;
            }

            /// The line of the first call about @p name, empty when there is none
            [[nodiscard]] String LineOf(StringView name) const {
                for (const String& line : log) {
                    if (line.find(Format(" {} ", name)) != String::npos) {
                        return line;
                    }
                }
                return {};
            }

        private:
            template <typename T>
            void Add(StringView kind, StringView name, const T& value) {
                String line = Format("{} {} {}", kind, name, value);
                if (!_fields.empty()) {
                    const FieldAttributes& field = _fields.back();
                    if (field.readOnly) {
                        line.append(" readonly");
                    }
                    if (field.hasRange) {
                        AppendFormat(line, " [{}, {}] by {}", field.rangeMin, field.rangeMax, field.step);
                    }
                    if (!field.tooltip.empty()) {
                        AppendFormat(line, " ({})", field.tooltip);
                    }
                }
                log.push_back(std::move(line));
            }

            template <typename T>
            bool Edit(StringView name, T& value, const T& edited) {
                if (!edits || eastl::find(kept.begin(), kept.end(), name) != kept.end()) {
                    return false;
                }
                value = edited;
                return true;
            }

            template <typename T>
            bool Number(StringView kind, StringView name, T& value) {
                Add(kind, name, value);
                return Edit(name, value, static_cast<T>(value == std::numeric_limits<T>::max() ? value - 1 : value + 1));
            }

            Vector<FieldAttributes> _fields;
            bool _canChangeList = false;
        };

        class RemoteFieldsTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                CoreManager::GetRendererManager().SetRenderer(New<RecordingRenderer>());
                XmlDeserializer deserializer;
                const String xml = SceneConfigText(kNodes);
                ASSERT_TRUE(deserializer.LoadFromString(xml.c_str()));
                _manager = New<SceneManager>();
                _manager->Deserialize(deserializer);
                ASSERT_FALSE(deserializer.HasErrors());
                _manager->Initialize();
                _scene = _manager->GetSceneByName("Main"_intern);
                ASSERT_NE(_scene, nullptr);
                FillProbe();
                RegisterSceneControl(_service, [this] { return _scene; });
                RegisterReflectionControl(_service);
                ASSERT_TRUE(_schema.Load(Result("schema")));
            }

            void TearDown() override {
                _manager.Reset();
                CoreManager::GetRendererManager().SetRenderer({});
            }

            IComponent& Local(StringView node, StringView type) {
                const auto found = FindSceneNodeByPath(_scene->GetRootNode(), node);
                for (const auto& component : Require(found.has_value() ? *found : nullptr).GetComponents()) {
                    if (component->GetTypeMeta().typeName == type) {
                        return *component;
                    }
                }
                return Require(static_cast<IComponent*>(nullptr));
            }

            void FillProbe() {
                auto& probe = static_cast<RemoteProbeComponent&>(Local("Probe", "RemoteProbeComponent"));
                probe._areas["a \"quoted\\ key"] = Rect{1.0f, 2.0f, 3.0f, 4.0f};
                VisitorContainersTestData::ContainerFields& containers = probe._containers;
                containers.numbers = {1.0f, 2.0f};
                containers.items.emplace_back().id = 6;
                containers.weights["w"] = 1.5f;
                containers.names[3] = "three";
                containers.hashed["h"] = 7;
                containers.small.push_back(8);
                containers.fixed.push_back(9);
                containers.entry = {10, "ten"};
                containers.maybe = 11;
                containers.choice.emplace<VisitorContainersTestData::ContainerItem>().id = 12;
                containers.locked.push_back(13);
                containers.secret.push_back(14);
            }

            /// The text of the result of @p method
            String Result(StringView method, StringView params = {}) {
                const ControlReply reply = CallControlText(_service, 1, method, params);
                EXPECT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
                return String(reply.Result().Raw());
            }

            String Get(StringView node, StringView type) {
                return Result("scene_get", Format(R"({{"node":"{}","component":"{}"}})", node, type));
            }

            RemoteComponent Read(StringView node, StringView type) {
                RemoteComponent component;
                EXPECT_TRUE(component.Load(Get(node, type), 0));
                EXPECT_TRUE(component.IsLoaded());
                return component;
            }

            Vector<String> LocalLog(StringView node, StringView type) {
                LogVisitor visitor;
                Local(node, type).AcceptPropertyVisitor(visitor);
                return visitor.log;
            }

            Vector<String> RemoteLog(RemoteComponent& component) {
                LogVisitor visitor;
                EXPECT_FALSE(component.Walk(_schema, _writes, visitor));
                return visitor.log;
            }

            /// Edits what @p visitor edits, then sends every write to `scene_set`; the paths it sent
            Vector<String> EditAndSend(StringView node, StringView type, LogVisitor& visitor) {
                RemoteComponent component = Read(node, type);
                visitor.edits = true;
                EXPECT_TRUE(component.Walk(_schema, _writes, visitor));
                _writes.ReleaseText();
                Vector<String> paths;
                while (const RemoteWrite* write = _writes.NextToSend()) {
                    String params;
                    JsonWriter writer(params);
                    writer.BeginObject();
                    writer.Member("node", StringView(write->node));
                    writer.Member("component", StringView(write->component));
                    writer.Member("index", static_cast<uint64_t>(write->index));
                    writer.Member("path", StringView(write->path));
                    writer.Key("value");
                    WritePropertyValue(writer, write->value);
                    writer.EndObject();
                    const ControlReply reply = CallControlText(_service, 1, "scene_set", params);
                    EXPECT_TRUE(reply.IsOk()) << write->path.c_str() << ": " << reply.ErrorMessage().c_str();
                    paths.push_back(write->path);
                    const uint64_t id = write->id;
                    _writes.MarkSent(id);
                    _writes.Accept(id, 0);
                }
                return paths;
            }

            static bool Has(const Vector<String>& paths, StringView path) {
                return eastl::find(paths.begin(), paths.end(), String(path)) != paths.end();
            }

            ControlService _service;
            IntrusivePtr<SceneManager> _manager;
            Scene* _scene = nullptr;
            RemoteSchema _schema;
            RemotePendingWrites _writes;
        };

    }  // namespace

    TEST_F(RemoteFieldsTest, ARemoteComponentIsVisitedAsTheLocalOne) {
        for (const StringView type : {"TransformComponent", "QuadRendererComponent", "SpriteRendererComponent", "TextComponent", "UICanvasComponent"}) {
            SCOPED_TRACE(String(type).c_str());
            RemoteComponent component = Read("Thing", type);
            const Vector<String> local = LocalLog("Thing", type);

            EXPECT_FALSE(local.empty());
            EXPECT_EQ(RemoteLog(component), local);
        }
    }

    TEST_F(RemoteFieldsTest, WhatTheWireOrAWriteCannotCarryIsShownReadOnly) {
        RemoteComponent probe = Read("Probe", "RemoteProbeComponent");
        LogVisitor visitor;

        probe.Walk(_schema, _writes, visitor);

        EXPECT_EQ(visitor.LineOf("level"), "int32 level 3 readonly");
        EXPECT_EQ(visitor.LineOf("percent"), "float percent 50 [0, 100] by 5 (Share of the whole)");
        EXPECT_EQ(visitor.LineOf("maybe"), "presence maybe true readonly");
        EXPECT_EQ(visitor.LineOf("choice"), "compound choice ");
        EXPECT_NE(eastl::find(visitor.log.begin(), visitor.log.end(), String("alternative choice 2 readonly")), visitor.log.end());
        EXPECT_FALSE(visitor.SawAChangeableList());
        EXPECT_TRUE(visitor.LineOf("secret").empty());

        RemoteComponent hud = Read("Hud", "UICanvasComponent");
        LogVisitor elements;
        hud.Walk(_schema, _writes, elements);
        // The fields of the class an element names, and a colour the wire writes as one text
        EXPECT_FALSE(elements.LineOf("spriteTint").empty());
        EXPECT_EQ(elements.LineOf("color"), "text color 1,2,3,255 readonly");
    }

    TEST_F(RemoteFieldsTest, EveryPathAnEditEmitsIsTakenBySceneSet) {
        for (const StringView type : {"TransformComponent", "QuadRendererComponent", "SpriteRendererComponent", "TextComponent"}) {
            SCOPED_TRACE(String(type).c_str());
            LogVisitor visitor;
            EXPECT_FALSE(EditAndSend("Thing", type, visitor).empty());
        }
        // A new `source` would replace the elements the later paths name
        LogVisitor canvas;
        canvas.kept = {"source"};
        const Vector<String> canvasPaths = EditAndSend("Hud", "UICanvasComponent", canvas);
        EXPECT_TRUE(Has(canvasPaths, "elements[0].name"));
        EXPECT_TRUE(Has(canvasPaths, "elements[0].children[0].opacity"));

        LogVisitor probe;
        const Vector<String> paths = EditAndSend("Probe", "RemoteProbeComponent", probe);
        for (const StringView path : {"percent", "cell.x", R"(areas["a \"quoted\\ key"].w)", "containers.numbers[1]", "containers.items[0].id", R"(containers.weights["w"])", "containers.names[3]",
                                      R"(containers.hashed["h"])", "containers.twin[0]", "containers.small[0]", "containers.fixed[0]", "containers.entry.first", "containers.entry.second",
                                      "containers.maybe", "containers.choice.value.id"}) {
            EXPECT_TRUE(Has(paths, path)) << String(path).c_str();
        }
        // `readonly` fields are visited and `hidden` ones are not: neither is written
        EXPECT_FALSE(Has(paths, "level"));
        EXPECT_FALSE(Has(paths, "containers.locked[0]"));
        EXPECT_FALSE(Has(paths, "containers.secret[0]"));
    }

    TEST_F(RemoteFieldsTest, AnEditIsShownUntilAReadSentAfterItsAnswerArrives) {
        RemoteComponent transform = Read("Thing", "TransformComponent");
        LogVisitor edit;
        edit.edits = true;
        edit.kept = {"y", "width", "height", "rotation", "pivotX", "pivotY"};
        ASSERT_TRUE(transform.Walk(_schema, _writes, edit));
        const auto shown = [&] {
            LogVisitor visitor;
            transform.Walk(_schema, _writes, visitor);
            return visitor.LineOf("x");
        };
        EXPECT_EQ(shown(), "float x 2.5");

        const uint64_t first = Require(_writes.NextToSend()).id;
        _writes.MarkSent(first);
        // A newer value of the same field waits for the answer to the one in flight
        ASSERT_TRUE(transform.Walk(_schema, _writes, edit));
        EXPECT_EQ(_writes.NextToSend(), nullptr);
        _writes.Accept(first, 7);
        const RemoteWrite& second = Require(_writes.NextToSend());
        EXPECT_EQ(std::get<double>(second.value), 3.5);
        const uint64_t secondId = second.id;
        _writes.MarkSent(secondId);
        _writes.Accept(secondId, 9);

        _writes.ReleaseComponent("Thing", "TransformComponent", 0, 9);
        EXPECT_EQ(shown(), "float x 3.5");
        _writes.ReleaseComponent("Thing", "TransformComponent", 1, 10);
        _writes.ReleaseComponent("Hud", "TransformComponent", 0, 10);
        EXPECT_EQ(shown(), "float x 3.5");
        _writes.ReleaseComponent("Thing", "TransformComponent", 0, 10);
        EXPECT_EQ(shown(), "float x 1.5");
    }

    TEST_F(RemoteFieldsTest, ATextWaitsUntilNoFieldIsBeingEditedAndANewerValueReplacesAQueuedOne) {
        const RemoteWriteTarget name{"Thing", "TextComponent", 0, "fontFamily"};
        const RemoteWriteTarget weight{"Thing", "TextComponent", 0, "fontWeight"};
        _writes.Edit(name, String("Rus"), RemoteEditKind::Text);
        EXPECT_EQ(_writes.NextToSend(), nullptr);
        _writes.Edit(weight, uint64_t{100}, RemoteEditKind::Value);
        _writes.Edit(weight, uint64_t{200}, RemoteEditKind::Value);
        ASSERT_EQ(_writes.GetWrites().size(), 2u);
        EXPECT_EQ(std::get<uint64_t>(Require(_writes.NextToSend()).value), 200u);

        _writes.ReleaseText();
        EXPECT_EQ(Require(_writes.NextToSend()).path, "fontFamily");
        // Typing goes on: the text is held again
        _writes.Edit(name, String("Russo"), RemoteEditKind::Text);
        EXPECT_EQ(Require(_writes.NextToSend()).path, "fontWeight");
        EXPECT_EQ(std::get<String>(Require(_writes.Find(name))), "Russo");
    }

    TEST_F(RemoteFieldsTest, ARefusedWriteDropsItsOverrideAndKeepsTheReason) {
        const RemoteWriteTarget name{"Thing", "TextComponent", 0, "fontFamily"};
        _writes.Edit(name, String("Nobody"), RemoteEditKind::Text);
        _writes.ReleaseText();
        const uint64_t id = Require(_writes.NextToSend()).id;
        _writes.MarkSent(id);

        _writes.Refuse(id, "interning", "only a known text");

        EXPECT_EQ(_writes.Find(name), nullptr);
        const RemoteRefusal& refusal = Require(_writes.GetRefusal());
        EXPECT_EQ(refusal.path, "fontFamily");
        EXPECT_EQ(refusal.code, "interning");
        EXPECT_EQ(refusal.message, "only a known text");
        _writes.Clear();
        EXPECT_FALSE(_writes.GetRefusal().has_value());
    }

    TEST_F(RemoteFieldsTest, ALayerIsShownUntilATreeSentAfterItsAnswerArrives) {
        const RemoteWriteTarget layer{"Thing"};
        _writes.Edit(layer, int64_t{4}, RemoteEditKind::Value);
        const uint64_t id = Require(_writes.NextToSend()).id;
        EXPECT_TRUE(Require(_writes.NextToSend()).IsLayer());
        _writes.MarkSent(id);
        _writes.ReleaseLayers(100);
        EXPECT_NE(_writes.Find(layer), nullptr);
        _writes.Accept(id, 3);
        _writes.ReleaseComponent("Thing", "", 1, 100);

        _writes.ReleaseLayers(4);

        EXPECT_EQ(_writes.Find(layer), nullptr);
    }

    TEST_F(RemoteFieldsTest, ValuesThatDoNotFitTheirTypesAreLeftOut) {
        RemoteComponent component;
        LogVisitor visitor;
        ASSERT_TRUE(component.Load(R"({"node":"n","component":"RemoteProbeComponent","fields":{"level":"3","percent":1e999,"cell":{"x":1},"areas":{"key":1},)"
                                   R"("containers":{"numbers":[true,null,"x",[]],"items":[5,null],"twin":{},"entry":[],"maybe":"x","choice":{"index":9},"weights":[{"key":"k","value":[]}]}}})",
                                   0));

        component.Walk(_schema, _writes, visitor);

        EXPECT_TRUE(_writes.GetWrites().empty());
        EXPECT_TRUE(visitor.LineOf("level").empty());
        EXPECT_TRUE(visitor.LineOf("percent").empty());
        // An object that is no known group of numbers is a text
        EXPECT_FALSE(visitor.LineOf("cell").empty());
        for (const StringView text : {"", "{", "[]", R"({"node":"n"})", R"({"node":"n","component":"TransformComponent","fields":[]})", R"({"node":1,"component":"TransformComponent","fields":{}})",
                                      R"({"node":"n","component":"Unheard","fields":{"x":1}})"}) {
            SCOPED_TRACE(String(text).c_str());
            component.Load(text, 0);
            LogVisitor nothing;
            EXPECT_FALSE(component.Walk(_schema, _writes, nothing));
            EXPECT_TRUE(nothing.log.empty());
        }
    }

    TEST_F(RemoteFieldsTest, TypesThatHoldEachOtherEndAtTheDepthLimit) {
        ASSERT_TRUE(_schema.Load(R"({"types":[{"name":"Loop","fields":[{"name":"value","type":{"kind":"object","object":"Loop"}},)"
                                 R"({"name":"next","type":{"kind":"pointer","object":"Loop"}},{"name":"all","type":{"kind":"list","element":{"kind":"object","object":"Loop"}}}]}]})"));
        constexpr size_t kNesting = 100;
        String fields = R"({"all":[7],"next":)";
        for (size_t i = 0; i < kNesting; ++i) {
            fields.append(R"({"next":)");
        }
        fields.append("null");
        fields.append(kNesting, '}');
        fields.push_back('}');
        RemoteComponent component;
        ASSERT_TRUE(component.Load(Format(R"({{"node":"n","component":"Loop","fields":{}}})", fields), 0));
        LogVisitor visitor;

        component.Walk(_schema, _writes, visitor);

        const size_t compounds = eastl::count_if(visitor.log.begin(), visitor.log.end(), [](const String& line) { return line.find("compound") == 0; });
        EXPECT_GT(compounds, RemoteFieldWalker::kMaxDepth / 2);
        EXPECT_LE(compounds, 2 * RemoteFieldWalker::kMaxDepth + 2);
    }

    TEST_F(RemoteFieldsTest, ASecondWalkAllocatesNothing) {
        RemoteComponent probe = Read("Probe", "RemoteProbeComponent");
        RemoteComponent hud = Read("Hud", "UICanvasComponent");
        NullPropertyVisitor visitor;
        const String text = Get("Probe", "RemoteProbeComponent");
        _writes.Edit({"Probe", "RemoteProbeComponent", 0, "containers.entry.second"}, String("a text longer than the inline buffer of a string"), RemoteEditKind::Text);
        probe.Walk(_schema, _writes, visitor);
        hud.Walk(_schema, _writes, visitor);

        ExpectNoAllocations([&] {
            probe.Load(text, 0);
            probe.Walk(_schema, _writes, visitor);
            hud.Walk(_schema, _writes, visitor);
        });
    }

}  // namespace BECore::Tests
