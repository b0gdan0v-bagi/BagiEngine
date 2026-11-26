// The Viewport in remote mode: the widget over the fake program of FakeControlEndpoint, drawn in an ImGui context with no backend. What it shows is
// read back from the draw data, by the handle of the texture it made.

#include <BECore/FakeControlEndpoint.h>
#include <BECore/RefCounted/New.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/RemoteEditor/RemoteSessionRegistry.h>
#include <Widgets/RemoteEditor/RemoteViewportView.h>
#include <Widgets/Viewport/ViewportWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSteadyFrames = 10;
        /// Long enough per frame for the next picture to fall due within kSettleFrames
        constexpr std::chrono::milliseconds kFrameStep{100};
        constexpr int kSettleFrames = 8;
        constexpr StringView kAddress = "ws://127.0.0.1:8765";
        constexpr StringView kNodes = R"(<node name="Board"><components><component type="TransformComponent" width="80" height="60"/><component type="QuadRendererComponent"/></components></node>)";
        /// A picture far wider than the window, which has bars above and below it
        constexpr uint32_t kPictureWidth = 400;
        constexpr uint32_t kPictureHeight = 100;
        /// A fraction of the program's screen inside "Board"
        constexpr float kOnBoard = 0.05f;

        /// A texture that can be told from another in the draw data
        class HandleTexture final : public RecordedTexture {
        public:
            using RecordedTexture::RecordedTexture;

            void* GetNativeHandle() const override {
                return const_cast<HandleTexture*>(this);
            }
        };

        /// Keeps every texture it made, so a test sees whether somebody else still holds one
        class TextureTrackingRenderer final : public RecordingRenderer {
        public:
            IntrusivePtr<ITexture> CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) override {
                made.emplace_back(New<HandleTexture>(Vector<uint8_t>(rgba, rgba + size_t{width} * height * 4), width, height));
                return made.back();
            }

            void WaitIdle() override {
                heldAtWait = !made.empty() && IsHeld(made.size() - 1);
                RecordingRenderer::WaitIdle();
            }

            /// Somebody but this renderer holds the texture @p index
            [[nodiscard]] bool IsHeld(size_t index) const {
                return made[index].GetRefCount() > 1;
            }

            Vector<IntrusivePtr<ITexture>> made;
            /// The newest texture was still held when the GPU was last waited for
            bool heldAtWait = false;
        };

        /// The quad of a texture in the last frame drawn
        struct DrawnPicture {
            Rect rect;
            ImU32 tint = 0;
        };

    }  // namespace

    class RemoteViewportTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _endpoint = MakeUnique<FakeControlEndpoint>(kNodes);
            _renderer = New<TextureTrackingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            _endpoint->DrawScreen();
            _endpoint->SetScreen(kPictureWidth, kPictureHeight, 200);
            _session = MakeUnique<RemoteSession>(_endpoint->Factory());
            _sessionHandle = RemoteSessionRegistry::GetInstance().Register(*_session);

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            _fonts = LoadImGuiFonts(io, _buffers);
            SetActiveImGuiFonts(_fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);

            _viewport = New<ViewportWidget>();
            XmlDeserializer widgetConfig;
            ASSERT_TRUE(_viewport->Initialize(widgetConfig));
        }

        void TearDown() override {
            _viewport.Reset();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            _sessionHandle.Reset();
            _session.reset();
            _endpoint.reset();
            _renderer.Reset();
        }

        void DrawFrame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            _viewport->Update();
            ImGui::Render();
        }

        /// A frame of the editor: the link is polled and the program answers, then the Viewport draws
        void Frame() {
            _session->Poll(_now += kFrameStep);
            _endpoint->Serve();
            _session->Poll(_now);
            DrawFrame();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        void AttachAndShow() {
            ASSERT_TRUE(_session->Attach(kAddress).has_value());
            Settle();
            ASSERT_EQ(_session->GetState(), RemoteSessionState::Attached);
            ASSERT_EQ(_renderer->made.size(), 1u);
        }

        /// The quad drawn with the texture @p index; nothing when the last frame drew none
        [[nodiscard]] Optional<DrawnPicture> Drawn(size_t index) const {
            const auto texture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(At(_renderer->made, index)->GetNativeHandle()));
            for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
                for (const ImDrawCmd& command : list->CmdBuffer) {
                    if (command.TexRef._TexData != nullptr || command.TexRef._TexID != texture || command.ElemCount == 0) {
                        continue;
                    }
                    const ImDrawVert& first = list->VtxBuffer[command.VtxOffset + list->IdxBuffer[command.IdxOffset]];
                    ImVec2 min = first.pos;
                    ImVec2 max = first.pos;
                    for (unsigned int i = 0; i < command.ElemCount; ++i) {
                        const ImDrawVert& vertex = list->VtxBuffer[command.VtxOffset + list->IdxBuffer[command.IdxOffset + i]];
                        min = ImVec2(eastl::min(min.x, vertex.pos.x), eastl::min(min.y, vertex.pos.y));
                        max = ImVec2(eastl::max(max.x, vertex.pos.x), eastl::max(max.y, vertex.pos.y));
                    }
                    return DrawnPicture{{min.x, min.y, max.x - min.x, max.y - min.y}, first.col};
                }
            }
            return {};
        }

        void ClickPoint(float x, float y) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(x, y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Settle();
        }

        [[nodiscard]] size_t CountPicks() {
            size_t count = 0;
            for (const String& method : _endpoint->Connection().Methods()) {
                count += StringView(method) == "scene_pick" ? 1 : 0;
            }
            return count;
        }

        UniquePtr<FakeControlEndpoint> _endpoint;
        IntrusivePtr<TextureTrackingRenderer> _renderer;
        UniquePtr<RemoteSession> _session;
        RemoteSessionHandle _sessionHandle;
        RemoteSessionTime _now{};
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        IntrusivePtr<ViewportWidget> _viewport;
    };

    TEST(RemoteViewportFitTest, AWiderPictureHasBarsAboveAndBelowAndItsCornersAreTheEndsOfTheFractions) {
        const Rect image = FitRemotePicture({10.0f, 20.0f, 200.0f, 200.0f}, 100, 50);
        EXPECT_FLOAT_EQ(image.x, 10.0f);
        EXPECT_FLOAT_EQ(image.y, 70.0f);
        EXPECT_FLOAT_EQ(image.w, 200.0f);
        EXPECT_FLOAT_EQ(image.h, 100.0f);

        const RemotePicturePoint corner = Require(PointOfRemotePicture(image, 210.0f, 170.0f));
        EXPECT_DOUBLE_EQ(corner.x, 1.0);
        EXPECT_DOUBLE_EQ(corner.y, 1.0);
        const RemotePicturePoint inner = Require(PointOfRemotePicture(image, 60.0f, 70.0f));
        EXPECT_DOUBLE_EQ(inner.x, 0.25);
        EXPECT_DOUBLE_EQ(inner.y, 0.0);
        EXPECT_FALSE(PointOfRemotePicture(image, 60.0f, 69.0f).has_value()) << "a point of a bar is not a point of the picture";
    }

    TEST(RemoteViewportFitTest, APictureOrAnAreaWithNoSizeHasNoPlace) {
        EXPECT_TRUE(FitRemotePicture({0.0f, 0.0f, 100.0f, 100.0f}, 0, 50).IsEmpty());
        EXPECT_TRUE(FitRemotePicture({0.0f, 0.0f, 100.0f, -4.0f}, 100, 50).IsEmpty());
        EXPECT_FALSE(PointOfRemotePicture({}, 0.0f, 0.0f).has_value());
    }

    TEST_F(RemoteViewportTest, ATextureIsMadeForEachNewFrameAndTheOldOneIsLetGoWithTheGpuWaitedFor) {
        // What SetUp drew as the program's screen
        _renderer->ClearCalls();
        ASSERT_NO_FATAL_FAILURE(AttachAndShow());
        Settle();
        ASSERT_EQ(_renderer->made.size(), 1u) << "the same frame was made a texture again";
        const DrawnPicture picture = Require(Drawn(0));
        EXPECT_NEAR(picture.rect.w / picture.rect.h, static_cast<float>(kPictureWidth) / kPictureHeight, 0.05f);
        EXPECT_EQ(picture.tint, IM_COL32_WHITE);

        _endpoint->SetScreen(kPictureWidth, kPictureHeight, 20);
        // The first frames may pass before the next picture is due
        for (int i = 0; i < kSettleFrames && _renderer->made.size() == 1; ++i) {
            Frame();
        }
        ASSERT_EQ(_renderer->made.size(), 2u);
        EXPECT_FALSE(_renderer->IsHeld(0));
        EXPECT_GT(_renderer->waitIdleCalls, 0u);
        EXPECT_TRUE(Drawn(1).has_value());
        EXPECT_TRUE(_renderer->calls.empty()) << "a scene of this process was drawn";
    }

    TEST_F(RemoteViewportTest, AClickOnThePictureSelectsWhatTheProgramShowsThereAndAClickOnABarAsksNothing) {
        ASSERT_NO_FATAL_FAILURE(AttachAndShow());
        const Rect image = Require(Drawn(0)).rect;

        // Within the window's content, above the picture
        ASSERT_NO_FATAL_FAILURE(ClickPoint(image.x + image.w * 0.5f, image.y - 10.0f));
        EXPECT_EQ(CountPicks(), 0u);

        ASSERT_NO_FATAL_FAILURE(ClickPoint(image.x + image.w * kOnBoard, image.y + image.h * kOnBoard));
        EXPECT_EQ(CountPicks(), 1u);
        EXPECT_EQ(_session->GetSelection(), Optional<String>("Board"));

        ASSERT_NO_FATAL_FAILURE(ClickPoint(image.x + image.w * 0.9f, image.y + image.h * 0.9f));
        EXPECT_FALSE(_session->GetSelection().has_value()) << "the point of the click is not what was asked";
    }

    TEST_F(RemoteViewportTest, ALostLinkKeepsTheFrameGreyedAndTakesNoClick) {
        ASSERT_NO_FATAL_FAILURE(AttachAndShow());
        _endpoint->SetRefusing(true);
        _endpoint->Drop();
        Settle();
        ASSERT_EQ(_session->GetState(), RemoteSessionState::Lost);

        const DrawnPicture picture = Require(Drawn(0));
        EXPECT_NE(picture.tint, IM_COL32_WHITE);
        ASSERT_NO_FATAL_FAILURE(ClickPoint(picture.rect.x + picture.rect.w * kOnBoard, picture.rect.y + picture.rect.h * kOnBoard));
        // A click that was kept would be asked of the next connection
        _endpoint->SetRefusing(false);
        for (int i = 0; i < 20 * kSettleFrames && _session->GetState() != RemoteSessionState::Attached; ++i) {
            Frame();
        }
        ASSERT_EQ(_session->GetState(), RemoteSessionState::Attached);
        Settle();
        EXPECT_EQ(CountPicks(), 0u);
        EXPECT_FALSE(_session->GetSelection().has_value());
    }

    TEST_F(RemoteViewportTest, TheTextureIsGivenBackWhenTheEditorDetachesAndWithTheWidget) {
        ASSERT_NO_FATAL_FAILURE(AttachAndShow());
        _session->Detach();
        DrawFrame();
        EXPECT_FALSE(_renderer->IsHeld(0));
        EXPECT_TRUE(_renderer->heldAtWait) << "the texture was let go before the GPU was waited for";

        ASSERT_TRUE(_session->Attach(kAddress).has_value());
        Settle();
        ASSERT_EQ(_renderer->made.size(), 2u);
        ASSERT_TRUE(_renderer->IsHeld(1));
        _renderer->heldAtWait = false;
        _viewport.Reset();
        EXPECT_FALSE(_renderer->IsHeld(1));
        EXPECT_TRUE(_renderer->heldAtWait);
    }

    TEST_F(RemoteViewportTest, AFrameWithNoNewPictureAllocatesNothing) {
        // A refused pick adds a message, which is to cost nothing while it stays
        _endpoint->SetSceneOnScreen(false);
        ASSERT_NO_FATAL_FAILURE(AttachAndShow());
        const Rect image = Require(Drawn(0)).rect;
        ASSERT_NO_FATAL_FAILURE(ClickPoint(image.x + image.w * 0.5f, image.y + image.h * 0.5f));
        ASSERT_TRUE(_session->GetPickError().IsSet());
        for (int i = 0; i < kSettleFrames; ++i) {
            DrawFrame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                DrawFrame();
            }
        });
        EXPECT_EQ(_renderer->made.size(), 1u);
    }

}  // namespace BECore::Tests
