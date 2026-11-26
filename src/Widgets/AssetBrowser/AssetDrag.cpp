#include "AssetDrag.h"

#include <imgui.h>

namespace BECore {

    namespace {

        /// The dragged file, or null when no drag runs or it carries something else
        const AssetDragPayload* DraggedAsset(const ImGuiPayload* payload) {
            if (payload == nullptr || !payload->IsDataType(kAssetDragPayload) || payload->DataSize != static_cast<int>(sizeof(AssetDragPayload))) {
                return nullptr;
            }
            // The payload holds chars and a one byte enum, so the bytes ImGui keeps need no alignment
            static_assert(alignof(AssetDragPayload) == 1);
            return static_cast<const AssetDragPayload*>(payload->Data);
        }

    }  // namespace

    bool IsDraggableAsset(AssetKind kind) {
        switch (kind) {
            case AssetKind::Texture:
            case AssetKind::Font:
            case AssetKind::Sound:
            case AssetKind::UiDocument:
            case AssetKind::Node:
                return true;
            case AssetKind::None:
            case AssetKind::Scene:
            case AssetKind::Config:
            case AssetKind::Other:
                break;
        }
        return false;
    }

    Optional<AssetDragPayload> MakeAssetDragPayload(AssetKind kind, StringView path) {
        if (!IsDraggableAsset(kind) || path.empty() || path.size() >= kAssetDragPathSize) {
            return {};
        }
        AssetDragPayload payload;
        payload.kind = kind;
        std::copy_n(path.data(), path.size(), payload.path);
        return payload;
    }

    bool DrawAssetDragSource(AssetKind kind, StringView path, FunctionRef<void()> drawPreview) {
        const Optional<AssetDragPayload> payload = MakeAssetDragPayload(kind, path);
        if (!payload || !ImGui::BeginDragDropSource()) {
            return false;
        }
        ImGui::SetDragDropPayload(kAssetDragPayload, &*payload, sizeof(AssetDragPayload));
        drawPreview();
        ImGui::EndDragDropSource();
        return true;
    }

    Optional<AssetDragPayload> AcceptAssetDrop(AssetKind kind) {
        // BeginDragDropTarget does not look at the disabled flag
        if (kind == AssetKind::None || (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0) {
            return {};
        }
        // Looked at before the target opens: accepting is what draws the highlight
        const AssetDragPayload* dragged = DraggedAsset(ImGui::GetDragDropPayload());
        if (dragged == nullptr || dragged->kind != kind || !ImGui::BeginDragDropTarget()) {
            return {};
        }
        Optional<AssetDragPayload> result;
        if (const AssetDragPayload* delivered = DraggedAsset(ImGui::AcceptDragDropPayload(kAssetDragPayload))) {
            // Copied before the target ends: that is where ImGui drops the payload it delivered
            result = *delivered;
        }
        ImGui::EndDragDropTarget();
        return result;
    }

}  // namespace BECore
