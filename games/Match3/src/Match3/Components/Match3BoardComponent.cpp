#include "Match3BoardComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/Match3BoardComponent.gen.hpp>
#include <imgui.h>

namespace BECore::Match3 {

    namespace {

        constexpr Color kBoardBackground{40, 40, 50, 255};
        constexpr Color kCellLight{70, 70, 90, 255};
        constexpr Color kCellDark{55, 55, 75, 255};
        constexpr Color kSelectionHighlight{255, 255, 255, 200};
        constexpr Color kGemBorder{20, 20, 25, 255};

        constexpr Color GemColor(GemType g) noexcept {
            switch (g) {
                case GemType::Red:
                    return Color{220, 60, 60, 255};
                case GemType::Orange:
                    return Color{240, 140, 40, 255};
                case GemType::Yellow:
                    return Color{240, 220, 60, 255};
                case GemType::Green:
                    return Color{70, 200, 90, 255};
                case GemType::Blue:
                    return Color{70, 130, 230, 255};
                case GemType::Purple:
                    return Color{180, 80, 220, 255};
                case GemType::None:
                default:
                    return Color{0, 0, 0, 0};
            }
        }

    }  // namespace

    Match3BoardComponent* Match3BoardComponent::s_active = nullptr;

    Match3BoardComponent::~Match3BoardComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void Match3BoardComponent::OnAttached() {
        s_active = this;
        _model.Reset(_seed);
        Subscribe<SceneEvents::SceneDrawEvent, &Match3BoardComponent::OnDraw>(this);
        Subscribe<SDLEvents::MouseButtonDownEvent, &Match3BoardComponent::OnMouseDown>(this);
    }

    void Match3BoardComponent::OnDraw(const SceneEvents::SceneDrawEvent&) {
        auto& renderer = *CoreManager::GetRenderer();

        const float boardSide = _cellSize * static_cast<float>(kBoardSize);
        renderer.DrawFilledRect(_originX - 4.0f, _originY - 4.0f, boardSide + 8.0f, boardSide + 8.0f, kBoardBackground);

        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                const float px = _originX + static_cast<float>(col) * _cellSize;
                const float py = _originY + static_cast<float>(row) * _cellSize;
                const bool isLight = ((col + row) % 2) != 0;
                renderer.DrawFilledRect(px, py, _cellSize, _cellSize, isLight ? kCellLight : kCellDark);

                const GemType g = _model.GetCellAt(Cell{col, row});
                if (g != GemType::None) {
                    const float pad = _cellSize * 0.12f;
                    renderer.DrawFilledRect(px + pad - 2.0f, py + pad - 2.0f, _cellSize - pad * 2.0f + 4.0f, _cellSize - pad * 2.0f + 4.0f, kGemBorder);
                    renderer.DrawFilledRect(px + pad, py + pad, _cellSize - pad * 2.0f, _cellSize - pad * 2.0f, GemColor(g));
                }
            }
        }

        if (auto sel = _model.GetSelected()) {
            const float px = _originX + static_cast<float>(sel->col) * _cellSize;
            const float py = _originY + static_cast<float>(sel->row) * _cellSize;
            const float t = 3.0f;
            renderer.DrawFilledRect(px, py, _cellSize, t, kSelectionHighlight);
            renderer.DrawFilledRect(px, py + _cellSize - t, _cellSize, t, kSelectionHighlight);
            renderer.DrawFilledRect(px, py, t, _cellSize, kSelectionHighlight);
            renderer.DrawFilledRect(px + _cellSize - t, py, t, _cellSize, kSelectionHighlight);
        }

        ImGui::SetNextWindowSize(ImVec2(220.0f, 90.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Match3")) {
            ImGui::Text("Score: %d", _model.GetScore());
            if (ImGui::Button("Reset")) {
                _model.Reset(_seed);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("seed=%u", _seed);
        }
        ImGui::End();
    }

    void Match3BoardComponent::OnMouseDown(const SDLEvents::MouseButtonDownEvent& e) {
        const float boardSide = _cellSize * static_cast<float>(kBoardSize);
        const float localX = e.button.x - _originX;
        const float localY = e.button.y - _originY;
        if (localX < 0.0f || localY < 0.0f || localX >= boardSide || localY >= boardSide) {
            return;
        }

        const int col = static_cast<int>(localX / _cellSize);
        const int row = static_cast<int>(localY / _cellSize);
        if (!Cell::IsInside(col, row)) {
            return;
        }
        _model.OnCellClicked(Cell{static_cast<int8_t>(col), static_cast<int8_t>(row)});
    }

}  // namespace BECore::Match3
