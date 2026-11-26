#pragma once

#include <BECore/Resource/AssetKind.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <type_traits>

namespace BECore {

    /**
     * @file
     * The drag and drop of a content file between editor panels. A source is drawn with DrawAssetDragSource; a target accepts it with
     * AcceptAssetDrop, so no panel handles the ImGui payload itself.
     */

    /// The ImGui payload type of a dragged content file (`ImGui::SetDragDropPayload`, at most 32 characters)
    inline constexpr const char* kAssetDragPayload = "BE_ASSET";

    /// The room for the path of a dragged file, its terminator included
    inline constexpr size_t kAssetDragPathSize = 512;

    /// Which file is dragged. Trivially copyable, because ImGui copies the bytes
    struct AssetDragPayload {
        AssetKind kind = AssetKind::None;
        char path[kAssetDragPathSize] = {};  ///< virtual, such as `assets/fonts/lucide.ttf`
    };
    static_assert(std::is_trivially_copyable_v<AssetDragPayload>, "ImGui copies the payload as bytes");

    /// The kinds a drag can carry: those a field or the viewport can take
    [[nodiscard]] bool IsDraggableAsset(AssetKind kind);

    /// The payload of the file @p path, or none: a kind that is not draggable, an empty path, or one that does not fit (a cut path would name another file)
    [[nodiscard]] Optional<AssetDragPayload> MakeAssetDragPayload(AssetKind kind, StringView path);

    /**
     * @brief Makes the item drawn just before the drag source of the file @p path
     *
     * @p drawPreview draws what follows the pointer; it is called only while the item is dragged. A click without movement stays the item's own click.
     * A file with no payload (`MakeAssetDragPayload`) starts no drag.
     *
     * @return true while the item is dragged: the caller draws no tooltip of its own then, which would replace the preview
     */
    bool DrawAssetDragSource(AssetKind kind, StringView path, FunctionRef<void()> drawPreview);

    /**
     * @brief Makes the item drawn just before the drop target of a file of @p kind
     *
     * Called before anything that submits another item (a tooltip). A disabled item (`ImGui::BeginDisabled`), a file of another kind and `None` are no target:
     * nothing is highlighted and nothing is taken.
     *
     * @return the file released over the item in this frame, else none; a copy, because ImGui drops its bytes with the delivery
     */
    [[nodiscard]] Optional<AssetDragPayload> AcceptAssetDrop(AssetKind kind);

    /// AcceptAssetDrop() for a target that takes several kinds: those @p takes answers true for
    [[nodiscard]] Optional<AssetDragPayload> AcceptAssetDrop(FunctionRef<bool(AssetKind)> takes);

}  // namespace BECore
