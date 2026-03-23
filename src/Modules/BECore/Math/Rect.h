#pragma once

namespace BECore {

    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;

        constexpr Rect() = default;
        constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

        [[nodiscard]] constexpr bool IsEmpty() const {
            return w == 0.0f || h == 0.0f;
        }

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("x", x);
            s.WriteAttribute("y", y);
            s.WriteAttribute("w", w);
            s.WriteAttribute("h", h);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("x", x);
            d.ReadAttribute("y", y);
            d.ReadAttribute("w", w);
            d.ReadAttribute("h", h);
        }
    };

}  // namespace BECore
