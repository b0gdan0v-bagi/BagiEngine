#pragma once

struct ImFont;
struct ImGuiIO;

namespace BECore {

    /// Sizes in ImGui units before the global scales (style.FontScaleMain and FontScaleDpi): what PushFont(font, size) takes
    inline constexpr float kUiFontSize = 14.0f;
    inline constexpr float kMonoFontSize = 13.0f;
    /// The icon glyphs are drawn at this size inside the UI font (the merged source is rasterized at kIconFontSize / kUiFontSize)
    inline constexpr float kIconFontSize = 16.0f;
    /// Lucide glyphs sit a little low or high on the Inter text line; this lifts or lowers them (pixels at kUiFontSize)
    inline constexpr float kIconGlyphOffsetY = 0.0f;

    /// Virtual paths of the font files (the layered file system: a game's assets/fonts/ with the same name replaces the engine's)
    struct ImGuiFontPaths {
        const char* ui = "assets/fonts/Inter-Regular.ttf";
        const char* icons = "assets/fonts/lucide.ttf";
        const char* mono = "assets/fonts/JetBrainsMono-Regular.ttf";
    };

    /// The bytes of the font files. ImGui reads them for as long as its atlas lives (the sources do not own them), so
    /// they must outlive the ImGui context: destroy the context first.
    struct ImGuiFontBuffers {
        Vector<uint8_t> ui;
        Vector<uint8_t> icons;
        Vector<uint8_t> mono;
    };

    /// The two fonts of the editor. A null pointer is a font that could not be loaded; ImGui then keeps its own default.
    struct ImGuiFonts {
        ImFont* ui = nullptr;    ///< Inter Regular with the Lucide icons merged into it; the default font of the context
        ImFont* mono = nullptr;  ///< JetBrains Mono Regular, for numbers
    };

    /// The user scale of a window: the display scale the user set over the pixel density of the window (2 / 2 on macOS is 1,
    /// 1.5 / 1 on a window without a high-density flag is 1.5). ImGui makes crisp glyphs for the density by itself; this is
    /// what is left, and it goes into style.FontScaleDpi. A value that is zero, negative or not a number gives 1.
    float FontScaleFor(float displayScale, float pixelDensity);

    /// Adds Inter + Lucide (the default font) and JetBrains Mono to the atlas of the current ImGui context. A file that cannot
    /// be read is logged once and its font is left out; the buffers are filled for the files that were read.
    ImGuiFonts LoadImGuiFonts(ImGuiIO& io, ImGuiFontBuffers& buffers, const ImGuiFontPaths& paths = {});

    /// The fonts of the live ImGui context (set by ImGuiWidget, empty without one), so a panel can ask for the mono font
    const ImGuiFonts& GetImGuiFonts();
    void SetActiveImGuiFonts(const ImGuiFonts& fonts);

    /// Draws with the mono font while it lives; does nothing when the font was not loaded. Needs a frame in progress.
    class ScopedMonoFont {
    public:
        ScopedMonoFont();
        explicit ScopedMonoFont(ImFont* mono);
        ~ScopedMonoFont();

        ScopedMonoFont(const ScopedMonoFont&) = delete;
        ScopedMonoFont& operator=(const ScopedMonoFont&) = delete;

    private:
        bool _pushed = false;
    };

}  // namespace BECore
