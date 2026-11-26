#include "RemoteViewportView.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/ScenePickControl.h>
#include <CoreSDL/SDLScreenshot.h>
#include <EASTL/algorithm.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/RemoteEditor/RemoteSession.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr const char* kImageHitArea = "##RemoteImage";
        /// What greys a frame of a link that is not up
        constexpr ImU32 kStaleTint = IM_COL32(100, 100, 100, 255);

        void DrawWarning(const String& text) {
            ImGui::PushStyleColor(ImGuiCol_Text, GetImGuiPalette().warning);
            ImGui::TextWrapped("%s %s", ICON_LC_TRIANGLE_ALERT, text.c_str());
            ImGui::PopStyleColor();
        }

        String PictureSentence(const RemoteCallError& error) {
            String text = error.code.empty() ? Format("No new picture: {}", error.message) : Format("No new picture ({}): {}", error.code, error.message);
            if (StringView(error.code) == ScreenshotErrors::kCaptureFailed) {
                text += ". A desktop game gives a picture with --headless or --backend=sdl-gpu";
            }
            return text;
        }

        String PickSentence(const RemoteCallError& error) {
            if (StringView(error.code) == ScenePickControlErrors::kNotScreenView) {
                return String("A click selects nothing: the program runs the editor, so its picture is the whole editor window");
            }
            return error.code.empty() ? Format("The click selected nothing: {}", error.message) : Format("The click selected nothing ({}): {}", error.code, error.message);
        }

    }  // namespace

    Rect FitRemotePicture(const Rect& area, uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || area.w <= 0.0f || area.h <= 0.0f) {
            return {};
        }
        const float pictureWidth = static_cast<float>(width);
        const float pictureHeight = static_cast<float>(height);
        const float scale = eastl::min(area.w / pictureWidth, area.h / pictureHeight);
        const float shownWidth = pictureWidth * scale;
        const float shownHeight = pictureHeight * scale;
        return {area.x + (area.w - shownWidth) * 0.5f, area.y + (area.h - shownHeight) * 0.5f, shownWidth, shownHeight};
    }

    Optional<RemotePicturePoint> PointOfRemotePicture(const Rect& image, float x, float y) {
        if (image.IsEmpty()) {
            return {};
        }
        const RemotePicturePoint point{static_cast<double>(x - image.x) / image.w, static_cast<double>(y - image.y) / image.h};
        if (point.x < 0.0 || point.x > 1.0 || point.y < 0.0 || point.y > 1.0) {
            return {};
        }
        return point;
    }

    void RemoteViewportView::Draw(RemoteSession& session) {
        _link.Update(session);
        UpdateMessages(session);
        UpdateTexture(session.GetPicture());

        // Without a frame the empty state below says it
        if (_texture && (session.GetState() != RemoteSessionState::Attached || _link.IsTroubled())) {
            DrawWarning(_link.GetText());
        }
        if (_pictureError.source.IsSet()) {
            DrawWarning(_pictureError.text);
        }
        if (_pickError.source.IsSet()) {
            DrawWarning(_pickError.text);
        }

        const ImVec2 area = ImGui::GetContentRegionAvail();
        const ImVec2 density = ImGui::GetIO().DisplayFramebufferScale;
        session.RequestPicture(static_cast<uint32_t>(eastl::max(eastl::max(area.x * density.x, area.y * density.y), 0.0f)));

        if (_texture) {
            DrawImage(session);
        } else {
            EmptyState(_link.IsTroubled() ? ICON_LC_UNPLUG : ICON_LC_PLUG, _link.GetText().c_str());
        }
    }

    void RemoteViewportView::Release() {
        if (_texture) {
            // A backend may free the image at once, while a frame in flight still samples it
            if (IRenderer* const renderer = CoreManager::GetRenderer().Get()) {
                renderer->WaitIdle();
            }
            _texture = {};
        }
        _revision = 0;
    }

    void RemoteViewportView::UpdateTexture(const RemotePicture& picture) {
        if (_revision == picture.GetRevision()) {
            return;
        }
        Release();
        _revision = picture.GetRevision();
        const RgbaImage& image = picture.GetImage();
        IRenderer* const renderer = CoreManager::GetRenderer().Get();
        if (renderer != nullptr && !image.pixels.empty()) {
            _texture = renderer->CreateTexture(image.pixels.data(), image.width, image.height);
        }
    }

    void RemoteViewportView::UpdateMessages(const RemoteSession& session) {
        const auto update = [](Message& message, const RemoteCallError& error, FunctionRef<String(const RemoteCallError&)> sentence) {
            if (message.source.code != error.code || message.source.message != error.message) {
                message.source = error;
                message.text = sentence(error);
            }
        };
        update(_pictureError, session.GetPicture().GetError(), &PictureSentence);
        update(_pickError, session.GetPickError(), &PickSentence);
    }

    void RemoteViewportView::DrawImage(RemoteSession& session) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 area = ImGui::GetContentRegionAvail();
        const RgbaImage& picture = session.GetPicture().GetImage();
        const Rect image = FitRemotePicture({origin.x, origin.y, area.x, area.y}, picture.width, picture.height);
        if (image.IsEmpty()) {
            return;
        }
        const ImVec2 min{image.x, image.y};
        const ImVec2 max{image.x + image.w, image.y + image.h};
        ImGui::SetCursorScreenPos(min);
        // An item with an id of its own: the press is always seen and never starts a window move
        ImGui::InvisibleButton(kImageHitArea, {image.w, image.h});
        if (ImGui::IsItemClicked()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (const Optional<RemotePicturePoint> point = PointOfRemotePicture(image, mouse.x, mouse.y)) {
                // Taken only while the link is up
                session.RequestPick(point->x, point->y);
            }
        }
        const ImTextureID textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(_texture->GetNativeHandle()));
        const ImU32 tint = session.GetState() == RemoteSessionState::Attached ? IM_COL32_WHITE : kStaleTint;
        ImGui::GetWindowDrawList()->AddImage(textureId, min, max, {0.0f, 0.0f}, {1.0f, 1.0f}, tint);
    }

}  // namespace BECore
