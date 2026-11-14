#include "Match3BoardComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Generated/Match3BoardComponent.gen.hpp>

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

        /// The rect scaled around its own centre; 1 gives the rect itself, exactly
        Rect ShrinkAroundCentre(const Rect& rect, float scale) {
            if (scale >= 1.0f) {
                return rect;
            }
            return Rect{rect.x + rect.w * (1.0f - scale) * 0.5f, rect.y + rect.h * (1.0f - scale) * 0.5f, rect.w * scale, rect.h * scale};
        }

        /// The gem of a cell whose top-left corner is (px, py): the dark border and the fill. A sprite smaller than a gem shrinks around the cell
        /// centre; a @p clip (the board rect) cuts both rects (what lies above the top edge is not drawn)
        void DrawGem(IRenderer& renderer, float px, float py, float cellSize, float scale, GemType gem, const Rect* clip) {
            const float pad = cellSize * 0.12f;
            const float side = cellSize - pad * 2.0f;
            const Rect border = ShrinkAroundCentre(Rect{px + pad - 2.0f, py + pad - 2.0f, side + 4.0f, side + 4.0f}, scale);
            const Rect fill = ShrinkAroundCentre(Rect{px + pad, py + pad, side, side}, scale);
            const auto draw = [&renderer, clip](const Rect& rect, const Color& color) {
                if (clip == nullptr) {
                    renderer.DrawFilledRect(rect.x, rect.y, rect.w, rect.h, color);
                } else if (const auto visible = ClipToBoard(rect, *clip)) {
                    renderer.DrawFilledRect(visible->x, visible->y, visible->w, visible->h, color);
                }
            };
            draw(border, kGemBorder);
            draw(fill, GemColor(gem));
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
        _animation.SetClearedSink([this](Cell cell, GemType gem) { OnGemCleared(cell, gem); });
        SubscribeInScene<SDLEvents::MouseButtonDownEvent, &Match3BoardComponent::OnMouseDown>(this);
        SubscribeInScene<InputEvents::ActionEvent, &Match3BoardComponent::OnAction>(this);
    }

    ParticleEmitterComponent* Match3BoardComponent::FindBurstEmitter() {
        if (!_burstLooked || _burstLookedFor != _burstNode) {
            _burstLooked = true;
            _burstLookedFor = _burstNode;
            _burstEmitter = {};
            if (!_burstNode.Empty()) {
                for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                    if (node->GetName() == _burstNode) {
                        _burstEmitter = node->GetComponent<ParticleEmitterComponent>();
                        break;
                    }
                }
                if (!_burstEmitter) {
                    LOG_WARNING("Match3: no node '{}' with a ParticleEmitterComponent in the scene; gems clear without a burst"_format(_burstNode.ToStringView()));
                }
            }
        }
        return _burstEmitter.Get();
    }

    void Match3BoardComponent::OnGemCleared(Cell cell, GemType gem) {
        if (ParticleEmitterComponent* emitter = FindBurstEmitter()) {
            const float x = _originX + (static_cast<float>(cell.col) + 0.5f) * _cellSize;
            const float y = _originY + (static_cast<float>(cell.row) + 0.5f) * _cellSize;
            emitter->Burst(x, y, ParticleEmitterComponent::kEffectBurstCount, GemColor(gem));
        }
    }

    void Match3BoardComponent::PublishUiVars() {
        const uint64_t version = _model.GetVersion();
        if (version == _publishedVersion) {
            return;
        }
        _publishedVersion = version;
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("score"_intern, _model.GetScore());
        vars.Set("seed"_intern, _seed);
    }

    void Match3BoardComponent::OnAction(const InputEvents::ActionEvent& event) {
        // The actions of the buttons in config/SceneConfig.xml
        const PoolString resetAction = "Match3Reset"_intern;
        const PoolString menuAction = "Match3Menu"_intern;
        if (event.action == resetAction) {
            _animation.Finish();
            _model.Reset(_seed);
        } else if (event.action == menuAction && !_menuScene.Empty()) {
            _animation.Finish();
            CoreManager::GetSceneManager().SetActiveScene(_menuScene);
        }
    }

    void Match3BoardComponent::OnDraw() {
        PublishUiVars();
        auto& renderer = *CoreManager::GetRenderer();

        // The sprites are the picture only while their run goes on for the version it was started for; any other change of the model is drawn at once
        if (!IsAnimating()) {
            _animation.Finish();
        }
        const bool animating = _animation.IsRunning();

        const float boardSide = _cellSize * static_cast<float>(kBoardSize);
        renderer.DrawFilledRect(_originX - 4.0f, _originY - 4.0f, boardSide + 8.0f, boardSide + 8.0f, kBoardBackground);

        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                const float px = _originX + static_cast<float>(col) * _cellSize;
                const float py = _originY + static_cast<float>(row) * _cellSize;
                const bool isLight = ((col + row) % 2) != 0;
                renderer.DrawFilledRect(px, py, _cellSize, _cellSize, isLight ? kCellLight : kCellDark);

                const GemType g = _model.GetCellAt(Cell{col, row});
                if (!animating && g != GemType::None) {
                    DrawGem(renderer, px, py, _cellSize, 1.0f, g, nullptr);
                }
            }
        }

        if (animating) {
            const Rect board{_originX, _originY, boardSide, boardSide};
            for (const GemSprite& sprite : _animation.GetSprites()) {
                DrawGem(renderer, _originX + sprite.position.x * _cellSize, _originY + sprite.position.y * _cellSize, _cellSize, sprite.scale, sprite.gem, &board);
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
    }

    void Match3BoardComponent::OnMouseDown(const SDLEvents::MouseButtonDownEvent& e) {
        const float boardSide = _cellSize * static_cast<float>(kBoardSize);
        const float localX = GetScene().ScreenToSceneX(e.button.x) - _originX;
        const float localY = GetScene().ScreenToSceneY(e.button.y) - _originY;
        if (localX < 0.0f || localY < 0.0f || localX >= boardSide || localY >= boardSide) {
            return;
        }

        const int col = static_cast<int>(localX / _cellSize);
        const int row = static_cast<int>(localY / _cellSize);
        if (!Cell::IsInside(col, row)) {
            return;
        }
        ClickCell(Cell{static_cast<int8_t>(col), static_cast<int8_t>(row)});
    }

    void Match3BoardComponent::ClickCell(Cell cell) {
        if (!cell.IsInside()) {
            return;
        }
        _animation.Finish();  // the model is at its final board already: this snaps the picture, then the click is handled as usual
        if (!_animate) {
            _model.OnCellClicked(cell);
            return;
        }

        const Match3Board before = _model.GetBoard();
        const uint64_t versionBefore = _model.GetVersion();
        const Optional<Cell> selected = _model.GetSelected();
        _model.OnCellClicked(cell);

        const SwapTrace* trace = _model.GetLastSwap();
        if (_model.GetVersion() == versionBefore + 1 && trace != nullptr && trace->version == _model.GetVersion()) {
            _animatedVersion = _model.GetVersion();
            _animation.Start(CoreManager::GetTweens(), before, *trace);
        } else if (_model.GetVersion() == versionBefore && selected && Match3GameModel::IsAdjacent(*selected, cell)) {
            // Two neighbours were picked and no match came of it: they slide to each other and back, on a board that stays as it was
            _animatedVersion = _model.GetVersion();
            _animation.StartRejected(CoreManager::GetTweens(), before, *selected, cell);
        }
    }

    bool Match3BoardComponent::IsAnimating() const {
        return _animation.IsRunning() && _animatedVersion == _model.GetVersion();
    }

    void Match3BoardComponent::SkipAnimation() {
        _animation.Finish();
    }

}  // namespace BECore::Match3
