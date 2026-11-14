#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/UI/UIValueText.h>

namespace BECore {

    /**
     * @brief A Color that a UI file writes in one attribute (`color="0,0,0,160"` or `color="#000000A0"`) instead of the child element a plain Color uses.
     *
     * It is a Color for everything else: it converts from one and is passed where one is expected.
     *
     * It may instead refer to a theme token (`color="@primary"`, UIThemes): `token` is the name and the four channels are the **resolved** value, which UIThemes::Resolve() writes
     * (transparent until the element is first drawn). Every reader of a colour reads the channels and needs no change. Assigning a plain Color drops the token; a token is
     * written back as `@name`, so a save keeps the reference.
     */
    struct UIColor : Color {
        PoolString token;  ///< the name of the theme token; empty: a literal colour

        constexpr UIColor() = default;
        constexpr UIColor(unsigned char r_, unsigned char g_, unsigned char b_, unsigned char a_) : Color(r_, g_, b_, a_) {}
        constexpr UIColor(const Color& color) : Color(color) {}  // NOLINT(google-explicit-constructor): a UI field takes a plain Color

        /// A reference to @p token, its channels transparent until UIThemes::Resolve()
        [[nodiscard]] static constexpr UIColor FromToken(PoolString token) {
            UIColor color;
            color.token = token;
            return color;
        }

        [[nodiscard]] constexpr bool HasToken() const {
            return !token.Empty();
        }

        /// The same channels and the same token. (ColorsEqual() looks at the channels only)
        [[nodiscard]] constexpr bool operator==(const UIColor& other) const {
            return r == other.r && g == other.g && b == other.b && a == other.a && token == other.token;
        }
    };

    /// @p color with every channel moved by @p delta (clamped to 0..255), the alpha kept: the lighter and darker states of a control are derived with it
    [[nodiscard]] inline Color ShiftColor(const Color& color, int delta) {
        const auto shift = [delta](unsigned char channel) { return static_cast<unsigned char>(std::clamp(static_cast<int>(channel) + delta, 0, 255)); };
        return {shift(color.r), shift(color.g), shift(color.b), color.a};
    }

    /// Whether two colors have the same four channels (Color has no `operator==`)
    [[nodiscard]] inline bool ColorsEqual(const Color& a, const Color& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    /// @p color with its alpha multiplied by @p factor (0..1): the disabled look of a control
    [[nodiscard]] inline Color WithAlpha(const Color& color, float factor) {
        return {color.r, color.g, color.b, static_cast<unsigned char>(static_cast<float>(color.a) * factor)};
    }

    /// The state shifts every control derives its looks from, so a button, a toggle and a slider agree on what hover, press and disabled look like
    inline constexpr int kHoverBrighten = 28;      ///< added to every channel under the pointer
    inline constexpr int kPressDarken = 36;        ///< taken from every channel while pressed (the pointer still over the control)
    inline constexpr float kDisabledAlpha = 0.5f;  ///< the share of its alpha a disabled control keeps

    /// The fill of a switched-on or filled part (the toggle's track while on, the progress bar's filled part, the slider's track before the thumb) when a file sets no `fillColor`.
    /// Shared by those controls; a control that wants another fallback adds a constant of its own
    inline constexpr Color kFillFallback{60, 160, 90, 255};

    namespace Ser {

        template <>
        struct DataAccessor<UIColor> {
            static void Save(ISerializer& s, const UIColor& value, StringView key) {
                const String text = UIText::FormatColorRef(value, value.token);
                s.WriteAttribute(key, StringView(text));
            }

            static bool Load(IDeserializer& d, UIColor& value, StringView key) {
                String text;
                if (!d.ReadAttribute(key, text)) {
                    return false;
                }
                const auto color = UIText::ParseColorRef(StringView(text));
                if (!color) {
                    d.ReportError(key, StringView(Format("Invalid color: '{}'", text).c_str()));
                    return false;
                }
                value = color->token.Empty() ? UIColor(color->color) : UIColor::FromToken(color->token);
                return true;
            }
        };

    }  // namespace Ser

    namespace PropVisit {

        /// The inspector edits the channels of a UIColor; an edit makes it a literal colour, so the token that would overwrite the channels at the next draw is dropped (an edit of a
        /// token is a decision to leave it). Like the Color it derives from, the channels are one group of four numbers
        inline bool Visit(IPropertyVisitor& v, UIColor& value, StringView key) {
            bool changed = false;
            if (v.BeginCompound(key)) {
                changed = value.VisitProperties(v);
                v.EndCompound();
            }
            if (changed) {
                value.token = PoolString{};
            }
            return changed;
        }

    }  // namespace PropVisit

}  // namespace BECore
