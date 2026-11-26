#include "SpriteSheetSlicePanel.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Math/NumberUtils.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Generated/SpriteSheetSlicePanel.gen.hpp>
#include <Widgets/ImGuiTheme.h>

namespace BECore {

    namespace {

        constexpr float kOverlayThickness = 1.0f;
        const char* const kPressFindIslands = "Press Find islands";

        /// Which names the library holds, order included: the plan depends on them, and nothing else in the library
        uint64_t NamesFingerprint(const TextureLibrary& library) {
            uint64_t print = library.GetAll().size();
            for (const SpriteEntry& entry : library.GetAll()) {
                HashCombine(print, entry.name.HashValue());
            }
            return print;
        }

        constexpr unsigned char kUtf8ContinuationMask = 0xC0;
        constexpr unsigned char kUtf8Continuation = 0x80;

        /// Cuts @p text to at most @p limit bytes, never inside a UTF-8 sequence; true when it was cut
        bool CutToBytes(String& text, size_t limit) {
            if (text.size() <= limit) {
                return false;
            }
            size_t end = limit;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & kUtf8ContinuationMask) == kUtf8Continuation) {
                --end;
            }
            text.resize(end);
            return true;
        }

        /// Clamps @p value to the range in place; true when it differs from @p current afterwards
        bool TakeNumber(int32_t& value, int minimum, int maximum, int& current) {
            value = std::clamp(value, minimum, maximum);
            if (value == current) {
                return false;
            }
            current = value;
            return true;
        }

        /// Copies @p text into the buffer @p target when it differs; true when it did
        template <size_t TSize>
        bool TakeText(const String& text, char (&target)[TSize]) {
            if (StringView(text) == StringView(target)) {
                return false;
            }
            const size_t length = std::min(text.size(), TSize - 1);
            memcpy(target, text.data(), length);
            target[length] = '\0';
            return true;
        }

    }  // namespace

    void SpriteSheetSlicePanel::Sync(SpriteSliceView& view, const TextureLibrary& library, PoolString texturePath, int sheetWidth, int sheetHeight) {
        _isOpen = view.open;
        const PlanKey key{texturePath, sheetWidth, sheetHeight, NamesFingerprint(library)};
        if (key.texturePath != _key.texturePath || key.width != _key.width || key.height != _key.height) {
            ClearIslands();
        }

        bool changed = false;
        if (view.mode != _mode) {
            _mode = view.mode;
            ClearIslands();
            changed = true;
        }
        changed |= TakeNumber(view.columns, 1, kMaxSheetGridAxis, _spec.columns);
        changed |= TakeNumber(view.rows, 1, kMaxSheetGridAxis, _spec.rows);
        changed |= TakeNumber(view.margin, 0, kMaxInsetPixels, _spec.margin);
        changed |= TakeNumber(view.spacing, 0, kMaxInsetPixels, _spec.spacing);
        bool islandsEdited = false;
        islandsEdited |= TakeNumber(view.alphaThreshold, 0, kMaxIslandAlphaThreshold, _island.alphaThreshold);
        islandsEdited |= TakeNumber(view.minSize, 1, kMaxIslandMinSize, _island.minSize);
        islandsEdited |= TakeNumber(view.padding, 0, kMaxIslandPadding, _island.padding);
        islandsEdited |= TakeNumber(view.mergeGap, 0, kMaxIslandMergeGap, _island.mergeGap);
        if (islandsEdited) {
            ClearIslands();
            changed = true;
        }
        changed |= TakeNumber(view.startIndex, 0, kMaxStartIndex, _startIndex);
        CutToBytes(view.prefix, kPrefixSize - 1);
        changed |= TakeText(view.prefix, _prefix);
        CutToBytes(view.names, kNamesSize - 1);
        changed |= TakeText(view.names, _names);
        if (view.policy != _policy) {
            _policy = view.policy;
            changed = true;
        }

        if (!changed && !_inputsChanged && key == _key) {
            return;
        }
        _key = key;
        _inputsChanged = false;
        MakePlan(library);
        view.summary.assign(_summary);
        view.hasError = _hasError;
        view.canAdd = _hasPlan && !_plan.entries.empty();
    }

    PoolString SpriteSheetSlicePanel::AddPlanned(TextureLibrary& library) {
        if (!_hasPlan || _plan.entries.empty()) {
            return {};
        }
        const PoolString firstAdded = _plan.entries.front().name;
        ApplyGridSlice(library, _plan);
        _inputsChanged = true;
        return firstAdded;
    }

    void SpriteSheetSlicePanel::DrawOverlay(ImDrawList* drawList, PoolString texturePath, ImVec2 origin, float scaleX, float scaleY) const {
        if (!_isOpen || _cells.empty() || _key.texturePath != texturePath) {
            return;
        }
        const ImU32 color = ImGui::GetColorU32(GetImGuiPalette().accentHover);
        for (const Rect& cell : _cells) {
            const ImVec2 cellMin(origin.x + cell.x / scaleX, origin.y + cell.y / scaleY);
            const ImVec2 cellMax(cellMin.x + cell.w / scaleX, cellMin.y + cell.h / scaleY);
            drawList->AddRect(cellMin, cellMax, color, 0.0f, 0, kOverlayThickness);
        }
    }

    void SpriteSheetSlicePanel::ClearIslands() {
        if (_mode == SheetSliceMode::Islands) {
            _cells.clear();
        }
        _islandError.clear();
        _inputsChanged = true;
    }

    void SpriteSheetSlicePanel::FindIslands(PoolString texturePath, int width, int height) {
        _cells.clear();
        _islandError.clear();
        _inputsChanged = true;

        const Path file = CoreManager::GetFileSystem().ResolvePath(texturePath.ToStringView());
        // The pixels live only in this call: the panel keeps the rects, never the image
        auto image = LoadRgbaImage(file);
        if (!image) {
            _islandError = image.error();
            return;
        }
        if (image->width != static_cast<uint32_t>(width) || image->height != static_cast<uint32_t>(height)) {
            _islandError = "The file changed since it was loaded: its size differs from the preview";
            return;
        }
        auto found = FindAlphaIslands(*image, _island);
        if (!found) {
            _islandError = String(SheetIslandErrorText(found.error()));
            return;
        }
        _cells = eastl::move(*found);
    }

    void SpriteSheetSlicePanel::SetStatus(StringView text, bool isError) {
        _hasError = isError;
        snprintf(_summary, sizeof(_summary), "%.*s", static_cast<int>(text.size()), text.data());
    }

    void SpriteSheetSlicePanel::MakePlan(const TextureLibrary& library) {
        _plan = GridSlicePlan{};
        _hasPlan = false;
        _hasError = false;

        if (_mode == SheetSliceMode::Grid) {
            _cells.clear();
            auto cells = ComputeGridCells(_key.width, _key.height, _spec);
            if (!cells) {
                SetStatus(SheetGridErrorText(cells.error()), true);
                return;
            }
            _cells = eastl::move(*cells);
        } else if (!_islandError.empty()) {
            SetStatus(StringView(_islandError), true);
            return;
        } else if (_cells.empty()) {
            SetStatus(kPressFindIslands, false);
            return;
        }

        SheetGridNaming naming;
        naming.prefix = String(StringView(_prefix));
        naming.startIndex = static_cast<uint32_t>(_startIndex);
        naming.names = ParseSheetNames(StringView(_names));

        auto plan = PlanRectSlice(library, _key.texturePath, _cells, naming, _policy);
        if (!plan) {
            SetStatus(SheetGridErrorText(plan.error()), true);
            return;
        }

        _plan = eastl::move(*plan);
        _hasPlan = true;
        const char* const noun = _mode == SheetSliceMode::Grid ? "cells" : "islands";
        const String summary = Format("{} {}: {} to add, {} to replace, {} skipped", _cells.size(), noun, _plan.entries.size() - _plan.replaced.size(), _plan.replaced.size(), _plan.skipped);
        SetStatus(StringView(summary), false);
    }

}  // namespace BECore
