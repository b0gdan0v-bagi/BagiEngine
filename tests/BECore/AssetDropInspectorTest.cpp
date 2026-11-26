#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Widgets/AssetBrowser/AssetDrag.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ComponentInspectors.h>
#include <Widgets/SceneEditor/ImGuiPropertyVisitor.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr ImVec2 kSourceWindowSize{200.0f, 200.0f};
        constexpr ImVec2 kTargetWindowPos{300.0f, 0.0f};
        constexpr ImVec2 kTargetWindowSize{800.0f, 880.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr const char* kSource = "drag source";

        AssetKind AssetOfField(StringView type, StringView field) {
            const TypeRegistry& registry = TypeRegistry::GetInstance();
            if (const TypeInfo* info = registry.Find(type)) {
                for (const FieldRef& ref : registry.AllFields(*info)) {
                    if (ref.field->name == field) {
                        return ref.field->attributes.asset;
                    }
                }
            }
            ADD_FAILURE() << "no field " << String(field).c_str() << " in " << String(type).c_str();
            return AssetKind::None;
        }

    }  // namespace

    /// A drag source in one window and an inspector in another, in an ImGui context with no backend, driven by the mouse
    class AssetDropInspectorTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            _fonts = LoadImGuiFonts(io, _buffers);
            SetActiveImGuiFonts(_fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            _index.Arm(*ImGui::GetCurrentContext());
        }

        void TearDown() override {
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
        }

        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kSourceWindowSize);
            ImGui::Begin("Source");
            ImGui::Button(kSource);
            DrawAssetDragSource(_dragKind, _dragPath, [] { ImGui::TextUnformatted("preview"); });
            ImGui::End();
            ImGui::SetNextWindowPos(kTargetWindowPos);
            ImGui::SetNextWindowSize(kTargetWindowSize);
            ImGui::Begin("Inspector");
            ImGui::BeginDisabled(_locked);
            if (_component != nullptr) {
                _changed |= RenderComponentInspector(_component);
            }
            if (_text != nullptr) {
                ImGuiPropertyVisitor visitor;
                visitor.BeginField(_textAttributes);
                _changed |= visitor.Visit("text", *_text);
                visitor.EndField();
            }
            ImGui::EndDisabled();
            ImGui::End();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// The first visible item whose label holds @p part
        const ImGuiItemRecord* Find(StringView part) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && StringView(item.label).find(part) != StringView::npos) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// ImGui gives the index no label for a closed combo: the sprite inspector's texture combo is the item drawn just before its rescan button
        const ImGuiItemRecord* TextureCombo() {
            const ImGuiItemRecord* previous = nullptr;
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && StringView(item.label).find("##tex_refresh") != StringView::npos) {
                    return previous;
                }
                previous = item.visible ? &item : previous;
            }
            return nullptr;
        }

        /// Drags a file from the source onto @p target and releases it there; returns whether the target showed the highlight before the release
        bool Drop(AssetKind kind, StringView path, const ImGuiItemRecord* target) {
            if (target == nullptr) {
                ADD_FAILURE() << "no target";
                return false;
            }
            const ImVec2 to(target->CenterX(), target->CenterY());
            _dragKind = kind;
            _dragPath = path;
            Settle();
            const ImGuiItemRecord* source = Find(kSource);
            if (source == nullptr) {
                ADD_FAILURE() << "no drag source";
                return false;
            }
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(source->CenterX(), source->CenterY());
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMousePosEvent(to.x, to.y);
            Settle();
            EXPECT_NE(ImGui::GetDragDropPayload(), nullptr) << "no drag runs";
            const bool highlighted = ImGui::IsDragDropPayloadBeingAccepted();
            io.AddMouseButtonEvent(0, false);
            Settle();
            return highlighted;
        }

        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        AssetKind _dragKind = AssetKind::None;
        StringView _dragPath;
        IComponent* _component = nullptr;
        String* _text = nullptr;
        FieldAttributes _textAttributes;
        bool _locked = false;
        bool _changed = false;
    };

    TEST_F(AssetDropInspectorTest, TheEnginesAssetPathFieldsSayWhichFileTheyTake) {
        EXPECT_EQ(AssetOfField("SpriteRendererComponent", "texturePath"), AssetKind::Texture);
        EXPECT_EQ(AssetOfField("ParticleEffect", "texturePath"), AssetKind::Texture);
        EXPECT_EQ(AssetOfField("AudioSourceComponent", "clip"), AssetKind::Sound);
        EXPECT_EQ(AssetOfField("TextComponent", "fontPath"), AssetKind::Font);
        EXPECT_EQ(AssetOfField("UITextComponent", "font"), AssetKind::Font);
        EXPECT_EQ(AssetOfField("UICanvasComponent", "source"), AssetKind::UiDocument);
    }

    TEST_F(AssetDropInspectorTest, AFileOfTheFieldsKindReplacesThePathAndReportsAChange) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        Settle();
        EXPECT_FALSE(_changed);

        EXPECT_TRUE(Drop(AssetKind::Sound, "assets/sounds/drop_probe.wav", Find("clip##")));
        EXPECT_EQ(audio->_clip.ToStringView(), "assets/sounds/drop_probe.wav");
        EXPECT_TRUE(_changed);
    }

    TEST_F(AssetDropInspectorTest, AFileOfAnotherKindIsNotTaken) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        Settle();

        EXPECT_FALSE(Drop(AssetKind::Texture, "assets/drop_probe.png", Find("clip##")));
        EXPECT_TRUE(audio->_clip.Empty());
        EXPECT_FALSE(_changed);
    }

    TEST_F(AssetDropInspectorTest, ALockedInspectorTakesNothing) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        _locked = true;
        Settle();

        EXPECT_FALSE(Drop(AssetKind::Sound, "assets/sounds/drop_probe.wav", Find("clip##")));
        EXPECT_TRUE(audio->_clip.Empty());
        EXPECT_FALSE(_changed);
    }

    TEST_F(AssetDropInspectorTest, ATextFieldTakesADropOnlyWithTheAttributeAndWhenItCanBeEdited) {
        String text = "old";
        _text = &text;
        Settle();
        EXPECT_FALSE(Drop(AssetKind::Font, "assets/fonts/drop_probe.ttf", Find("text##"))) << "a free text field";
        EXPECT_EQ(text, "old");

        _textAttributes = FieldAttributes{.readOnly = true, .asset = AssetKind::Font};
        EXPECT_FALSE(Drop(AssetKind::Font, "assets/fonts/drop_probe.ttf", Find("text##"))) << "a read-only field";
        EXPECT_EQ(text, "old");
        EXPECT_FALSE(_changed);

        _textAttributes.readOnly = false;
        EXPECT_TRUE(Drop(AssetKind::Font, "assets/fonts/drop_probe.ttf", Find("text##")));
        EXPECT_EQ(text, "assets/fonts/drop_probe.ttf");
        EXPECT_TRUE(_changed);
    }

    TEST_F(AssetDropInspectorTest, TheSpriteInspectorTakesATextureAndReloadsIt) {
        RecordingRenderer renderer;
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
        const uint8_t pixel[4] = {255, 255, 255, 255};
        sprite->SetTexture(renderer.CreateTexture(pixel, 1, 1));
        _component = sprite.Get();
        Settle();

        EXPECT_TRUE(Drop(AssetKind::Texture, "assets/drop_probe.png", TextureCombo()));
        EXPECT_EQ(sprite->_texturePath.ToStringView(), "assets/drop_probe.png");
        EXPECT_FALSE(sprite->GetTexture()) << "the component still draws the texture of the old path";
        EXPECT_TRUE(_changed);
    }

    TEST_F(AssetDropInspectorTest, TheSpriteInspectorTakesNoTextureWhileASpriteIdNamesOne) {
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
        sprite->_spriteId = "drop_probe_sprite"_intern;
        _component = sprite.Get();
        Settle();

        EXPECT_FALSE(Drop(AssetKind::Texture, "assets/drop_probe.png", TextureCombo()));
        EXPECT_TRUE(sprite->_texturePath.Empty());
        EXPECT_FALSE(_changed);
    }

}  // namespace BECore::Tests
