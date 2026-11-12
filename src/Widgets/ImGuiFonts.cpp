#include "ImGuiFonts.h"

#include <BECore/GameManager/CoreManager.h>
#include <Widgets/Icons/IconsLucide.h>
#include <cmath>
#include <imgui.h>

namespace BECore {

    namespace {

        /// The Lucide range of the header, zero-terminated; ImGui reads it while the font lives
        const ImWchar kIconGlyphRanges[] = {ICON_MIN_LC, ICON_MAX_16_LC, 0};

        ImGuiFonts activeFonts;

        bool ReadFontFile(const char* virtualPath, eastl::vector<uint8_t>& bytes) {
            bytes.clear();
            const auto realPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
            std::ifstream file(realPath, std::ios::binary | std::ios::ate);
            if (realPath.empty() || !file) {
                LOG_ERROR("ImGuiFonts: cannot open '{}'"_format(virtualPath));
                return false;
            }
            const auto size = static_cast<size_t>(file.tellg());
            bytes.resize(size);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
            if (!file || size == 0) {
                LOG_ERROR("ImGuiFonts: cannot read '{}'"_format(virtualPath));
                bytes.clear();
                return false;
            }
            return true;
        }

        /// The buffers stay with the caller: the sources do not own them
        ImFontConfig MakeSourceConfig(const char* name) {
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            snprintf(config.Name, sizeof(config.Name), "%s", name);
            return config;
        }

        ImFont* AddFontSource(ImGuiIO& io, eastl::vector<uint8_t>& bytes, float size, const ImFontConfig& config, const ImWchar* ranges) {
            return io.Fonts->AddFontFromMemoryTTF(bytes.data(), static_cast<int>(bytes.size()), size, &config, ranges);
        }

    }  // namespace

    float FontScaleFor(float displayScale, float pixelDensity) {
        if (!std::isfinite(displayScale) || !std::isfinite(pixelDensity) || displayScale <= 0.0f || pixelDensity <= 0.0f) {
            return 1.0f;
        }
        const float scale = displayScale / pixelDensity;
        return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
    }

    ImGuiFonts LoadImGuiFonts(ImGuiIO& io, ImGuiFontBuffers& buffers, const ImGuiFontPaths& paths) {
        BE_PROFILE_SCOPE("LoadImGuiFonts");
        ImGuiFonts fonts;
        const ImWchar* textRanges = io.Fonts->GetGlyphRangesCyrillic();

        if (ReadFontFile(paths.ui, buffers.ui)) {
            const ImFontConfig textConfig = MakeSourceConfig("Inter");
            fonts.ui = AddFontSource(io, buffers.ui, kUiFontSize, textConfig, textRanges);

            // Lucide goes into the same font: a label can hold a glyph and a word
            if (fonts.ui && ReadFontFile(paths.icons, buffers.icons)) {
                ImFontConfig iconConfig = MakeSourceConfig("Lucide");
                iconConfig.MergeMode = true;
                iconConfig.ExtraSizeScale = kIconFontSize / kUiFontSize;
                iconConfig.GlyphMinAdvanceX = kIconFontSize;
                iconConfig.GlyphOffset.y = kIconGlyphOffsetY;
                AddFontSource(io, buffers.icons, kUiFontSize, iconConfig, kIconGlyphRanges);
            }
        }

        if (ReadFontFile(paths.mono, buffers.mono)) {
            const ImFontConfig monoConfig = MakeSourceConfig("JetBrains Mono");
            fonts.mono = AddFontSource(io, buffers.mono, kMonoFontSize, monoConfig, textRanges);
        }
        return fonts;
    }

    const ImGuiFonts& GetImGuiFonts() {
        return activeFonts;
    }

    void SetActiveImGuiFonts(const ImGuiFonts& fonts) {
        activeFonts = fonts;
    }

    ScopedMonoFont::ScopedMonoFont() : ScopedMonoFont(activeFonts.mono) {}

    ScopedMonoFont::ScopedMonoFont(ImFont* mono) {
        if (mono) {
            ImGui::PushFont(mono, kMonoFontSize);
            _pushed = true;
        }
    }

    ScopedMonoFont::~ScopedMonoFont() {
        if (_pushed) {
            ImGui::PopFont();
        }
    }

}  // namespace BECore
