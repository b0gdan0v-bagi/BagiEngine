// The UI debug overlays of the viewport (Viewport/ViewportUIDebug), in an ImGui context with no backend.

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/Viewport/ViewportUIDebug.h>
#include <gtest/gtest.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{800.0f, 600.0f};

        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements><element type="UIPanel" name="Plate" x="10" y="10" width="200" height="100" color="10,20,30,200"/></elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        class ViewportUIDebugTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                CoreManager::GetRendererManager().SetRenderer(New<RecordingRenderer>());
                XmlDeserializer deserializer;
                ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
                _manager = New<SceneManager>();
                _manager->Deserialize(deserializer);
                ASSERT_FALSE(deserializer.HasErrors());
                _manager->Initialize();
                _manager->DrawAll();

                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = kDisplaySize;
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                SetActiveImGuiFonts(LoadImGuiFonts(io, _buffers));
            }

            void TearDown() override {
                _overlays.Release();
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
                _manager.Reset();
                CoreManager::GetRendererManager().SetRenderer({});
            }

            /// The vertices the element rects add to a draw list in one frame
            int DrawnVertices() {
                ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
                ImGui::NewFrame();
                ImDrawList& drawList = *ImGui::GetBackgroundDrawList();
                const ViewportFrame frame{.imageMin = {0.0f, 0.0f}, .imageMax = kDisplaySize, .origin = {0.0f, 0.0f}, .end = kDisplaySize};
                PreviewOverlays overlays;
                overlays.elementRects = true;
                _overlays.Collect(_manager->GetActiveScene(), [](const SceneNode*) { return true; });
                const int before = drawList.VtxBuffer.Size;
                _overlays.Draw(drawList, frame, overlays, false, {});
                const int drawn = drawList.VtxBuffer.Size - before;
                ImGui::Render();
                return drawn;
            }

            IntrusivePtr<UICanvasComponent> Canvas() const {
                return Require(_manager->GetActiveScene()).GetRootNode().GetChildren()[0]->GetComponent<UICanvasComponent>();
            }

            ImGuiFontBuffers _buffers;
            IntrusivePtr<SceneManager> _manager;
            ViewportUIDebug _overlays;
        };

    }  // namespace

    TEST_F(ViewportUIDebugTest, ADisabledCanvasGetsNoOverlayUntilItIsEnabledAgain) {
        ASSERT_GT(DrawnVertices(), 0);

        Require(Canvas()).SetEnabled(false);
        EXPECT_EQ(DrawnVertices(), 0);

        Require(Canvas()).SetEnabled(true);
        EXPECT_GT(DrawnVertices(), 0);
    }

}  // namespace BECore::Tests
