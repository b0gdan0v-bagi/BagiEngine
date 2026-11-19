#include <BECore/Renderer/ITexture.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/ArtGen/Sets/ArtGenSetPanel.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr StringView kFallbackName = "sprite";
        // "_" and the digits of a suffix: a name cut to kMaxSpriteNameLength - kSuffixRoom still has `_<n>` to spare
        constexpr size_t kSuffixRoom = 6;
        constexpr float kDragPreviewSize = 64.0f;

        template <size_t N>
        void CopyCut(char (&buffer)[N], StringView text) {
            const size_t length = std::min(text.size(), N - 1);
            std::copy_n(text.data(), length, buffer);
            buffer[length] = '\0';
        }

        ArtGenDropPlan Refuse(String reason) {
            ArtGenDropPlan plan;
            plan.reason = std::move(reason);
            return plan;
        }

        String InvalidNameReason(StringView name) {
            return Format("'{}' is not a sprite name: 1 to {} of A-Z a-z 0-9 _ -", name, kMaxSpriteNameLength);
        }

        /// @p base when the library has no sprite of that name, else `base_2`, `base_3`, ... through `MakeUniqueName`, with the base cut first
        /// so the suffix does not make a valid name too long (the cut may itself be free: then it is the result). Only a taken name reaches
        /// `MakeUniqueName`, which interns its candidates
        String FreeNameIn(const TextureLibrary& library, StringView base) {
            // Find, not Intern: a name typed live in a popup must not fill the pool with every half-typed text
            const Optional<PoolString> taken = PoolString::Find(base);
            if (!taken || library.GetSprite(*taken) == nullptr) {
                return String(base);
            }
            const StringView cut = base.substr(0, std::min(base.size(), kMaxSpriteNameLength - kSuffixRoom));
            return String(library.MakeUniqueName(cut).ToStringView());
        }

    }  // namespace

    ArtGenDragPayload MakeArtGenDragPayload(ArtGenDragKind kind, int64_t seed, StringView set, StringView job, StringView suggestedName) {
        ArtGenDragPayload payload;
        payload.kind = kind;
        payload.seed = seed;
        CopyCut(payload.set, set);
        CopyCut(payload.job, job);
        CopyCut(payload.suggestedName, suggestedName);
        return payload;
    }

    String SuggestedArtGenSpriteName(StringView job, StringView subject) {
        if (!job.empty()) {
            return String(job);
        }
        String name = ArtGenNameFromSubject(subject);
        return name.empty() ? String(kFallbackName) : name;
    }

    void DrawArtGenVariantDragSource(FunctionRef<ArtGenDragPayload()> makePayload, const ITexture* thumbnail) {
        if (!ImGui::BeginDragDropSource()) {
            return;
        }
        // Made only while a drag runs: the name of a run variant is a String, and a steady frame allocates nothing
        const ArtGenDragPayload payload = makePayload();
        ImGui::SetDragDropPayload(kArtGenVariantPayload, &payload, sizeof(payload));
        const auto& palette = GetImGuiPalette();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(kDragPreviewSize, kDragPreviewSize));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        DrawArtGenChecker(drawList, origin, kDragPreviewSize, ImGui::GetColorU32(palette.panelBg), ImGui::GetColorU32(palette.frameBg));
        if (thumbnail != nullptr && thumbnail->GetState() == ResourceState::Loaded) {
            const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(thumbnail->GetNativeHandle()));
            drawList->AddImage(id, origin, ImVec2(origin.x + kDragPreviewSize, origin.y + kDragPreviewSize));
        }
        ImGui::TextUnformatted(payload.suggestedName);
        ImGui::EndDragDropSource();
    }

    ArtGenDropPlan PlanArtGenDrop(const ArtGenDragPayload& payload, StringView targetSprite, const TextureLibrary& library) {
        if (payload.kind == ArtGenDragKind::Audio) {
            return Refuse(String(kArtGenSoundIsNotASprite));
        }
        ArtGenDropPlan plan;
        if (!targetSprite.empty()) {
            if (!IsValidSpriteName(targetSprite)) {
                return Refuse(InvalidNameReason(targetSprite));
            }
            plan.mode = ArtGenDropMode::Replace;
            plan.sprite = String(targetSprite);
            return plan;
        }
        const StringView suggested(payload.suggestedName);
        if (!IsValidSpriteName(suggested)) {
            return Refuse(InvalidNameReason(suggested));
        }
        plan.mode = ArtGenDropMode::New;
        plan.sprite = FreeNameIn(library, suggested);
        return plan;
    }

}  // namespace BECore
