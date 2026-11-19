#pragma once

#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Events/EventBase.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <cstdint>
#include <type_traits>

namespace BECore {

    /**
     * @file
     * The drag and drop of a generated variant out of the Generate panel: the one contract every panel that takes a variant includes (the Sprite
     * Library today; an asset browser, an inspector texture field, the viewport and a clip field later). A target accepts the payload type
     * `kArtGenVariantPayload`, plans the drop with PlanArtGenDrop and emits ArtGenVariantDropEvent; the Generate panel answers it.
     *
     * The payload names the variant and never holds its file: the receiver gets the variant through the session, which looks it up alive, so a
     * payload that outlives its variant (a run replaced in mid-drag, a set gone) is an error line and never a read of a missing file.
     */

    /// The ImGui payload type of a dragged variant (`ImGui::SetDragDropPayload`, at most 32 characters)
    inline constexpr const char* kArtGenVariantPayload = "ARTGEN_VARIANT";

    CORE_ENUM(ArtGenDragKind, uint8_t, Image, Audio)

    /// Which variant is dragged. Trivially copyable, because ImGui copies the bytes; the names are fixed buffers cut to fit
    struct ArtGenDragPayload {
        ArtGenDragKind kind = ArtGenDragKind::Image;
        int64_t seed = 0;
        char set[kArtGenRowNameSize] = {};            // empty: a variant of the Generate tab's run
        char job[kArtGenRowNameSize] = {};            // the row of `set`
        char suggestedName[kArtGenRowNameSize] = {};  // the sprite name a drop for a new sprite starts from
    };
    static_assert(std::is_trivially_copyable_v<ArtGenDragPayload>, "ImGui copies the payload as bytes");

    /// The payload of the variant @p seed. @p set and @p job are empty for a variant of the Generate tab's run; a text longer than its
    /// buffer is cut
    [[nodiscard]] ArtGenDragPayload MakeArtGenDragPayload(ArtGenDragKind kind, int64_t seed, StringView set, StringView job, StringView suggestedName);

    /// The sprite name a dragged variant offers: the row's @p job name for a set row, else the name of @p subject (`ArtGenNameFromSubject`);
    /// `sprite` when the subject has no letter or digit. Always a valid sprite name
    [[nodiscard]] String SuggestedArtGenSpriteName(StringView job, StringView subject);

    CORE_ENUM(ArtGenDropMode, uint8_t, New, Replace, Refused)

    /// What a drop does
    struct ArtGenDropPlan {
        ArtGenDropMode mode = ArtGenDropMode::Refused;
        String sprite;  // the name Accept writes (New: free in the library; Replace: the target's own); empty when refused
        String reason;  // why it is refused; empty otherwise
    };

    /**
     * @brief The decision of a drop of @p payload. A non-empty @p targetSprite is a drop onto that sprite (Replace); an empty one is a drop
     * for a new sprite, named by `payload.suggestedName` and made free in @p library (`name_2`, ... kept within a valid name's length).
     *
     * Refused, with the reason: an audio variant (a sound is not a sprite), and a name that is not `IsValidSpriteName`, because Accept
     * writes `assets/artgen/<name>.png`.
     */
    [[nodiscard]] ArtGenDropPlan PlanArtGenDrop(const ArtGenDragPayload& payload, StringView targetSprite, const TextureLibrary& library);

    /// The answer of the panel that took the drop
    struct ArtGenDropResult {
        bool ok = false;
        String message = String("the Generate panel is not available");  // what a drop nobody answered says; the answering panel replaces it
    };

    /**
     * @brief A panel took a dragged variant and asks the Generate panel to accept it as @p sprite.
     *
     * Emitted, not enqueued, so @p result is filled when `Emit` returns; with no subscriber it keeps its default (not ok). The Generate panel
     * owns the form's accept settings and the message line, which is why a target asks instead of calling the session. @p sprite is valid for
     * the call only. A target plans the drop first (PlanArtGenDrop) and sends the planned name.
     */
    struct ArtGenVariantDropEvent : public EventBase<ArtGenVariantDropEvent> {
        ArtGenDragPayload payload;
        StringView sprite;
        ArtGenDropResult* result = nullptr;

        ArtGenVariantDropEvent() = default;
        ArtGenVariantDropEvent(const ArtGenDragPayload& p, StringView s, ArtGenDropResult* r) : payload(p), sprite(s), result(r) {}
    };

}  // namespace BECore
