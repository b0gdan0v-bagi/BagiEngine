#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <cstdint>
#include <expected>

namespace BECore {

    class UIStrings;

    /// The language a screen reads: the effective language and the one a missing string falls back to. `UIStrings` owns it
    struct UILanguageView {
        BE_CLASS(UILanguageView)

        BE_REFLECT_FIELD PoolString language;
        BE_REFLECT_FIELD PoolString defaultLanguage;
    };

    /// The window metrics a screen lays out by: the part of `UILayoutContext` that a text or a row may depend on. `UICanvasComponent` owns it
    struct UIWindowView {
        BE_CLASS(UIWindowView)

        BE_REFLECT_FIELD float outputWidth = 0.0f;
        BE_REFLECT_FIELD float outputHeight = 0.0f;
        BE_REFLECT_FIELD float pixelDensity = 1.0f;
        BE_REFLECT_FIELD float displayScale = 1.0f;
        BE_REFLECT_FIELD float userScale = 1.0f;
        BE_REFLECT_FIELD Rect safeArea;
    };

    /// The levels `UIAmbient::Declare` added to a declaration
    struct UIAmbientLevels {
        LevelIndex language = 0;
        LevelIndex window = 0;
    };

    /**
     * @brief The language and the window size as levels of a game's `Context`, so a `Derived` screen value reads them like any other level
     *
     * The paths are `language.language` and `window.outputWidth`. The language level moves with the version of `UIStrings` (every change of a string moves it too: versions are per
     * level, not per field); the window level moves with the version counter of the canvas that owns the view, exactly when its layout context changes.
     */
    namespace UIAmbient {

        inline constexpr StringView kLanguageLevel = "language";
        inline constexpr StringView kWindowLevel = "window";

        /// Adds the levels `language` and `window` to @p declaration; the error of `ContextDecl::Add` (a duplicate name, too many levels) when it refuses one
        [[nodiscard]] std::expected<UIAmbientLevels, LinkError> Declare(ContextDecl& declaration);

        /// Puts the views at their levels. The objects and the counters must outlive @p context; call it again only when one of them is replaced, not every frame
        void Fill(Context& context, const UIAmbientLevels& levels, const UIStrings& strings, const UIWindowView& window, const uint64_t& windowVersion);

    }  // namespace UIAmbient

}  // namespace BECore
