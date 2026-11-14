#pragma once

#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UICanvasScaler.h>
#include <BECore/UI/UIRectSpec.h>

namespace BECore::Fuzz::UiGeometryInput {

    // =============================================================================
    // FuzzUiGeometry: the input is a stream of rounds. A round is a fixed number of fields, each read as the raw bits of a float (4 bytes, little endian: NaN, infinity and
    // denormals appear) or as a byte; a stream that ends early reads zeros. The same description (`Transfer`) is read by the target and written by the seed maker, so the two
    // cannot drift apart.
    // =============================================================================

    /// Rounds run from one input at most: a round is some hundreds of numbers and a few layouts
    inline constexpr size_t kMaxRounds = 48;

    inline constexpr size_t kMaxStackChildren = 6;
    inline constexpr size_t kMaxCanvasChildren = 4;
    inline constexpr size_t kMaxGrandChildren = 3;

    /// Reads fields from the bytes of an input
    class Reader {
    public:
        Reader(const uint8_t* data, size_t size) : _data(data), _size(size) {}

        [[nodiscard]] bool Done() const {
            return _position >= _size;
        }

        void Float(float& value) {
            uint32_t bits = 0;
            for (size_t i = 0; i < sizeof(bits); ++i) {
                uint8_t byte = 0;
                Byte(byte);
                bits |= static_cast<uint32_t>(byte) << (8 * i);
            }
            std::memcpy(&value, &bits, sizeof(value));
        }

        void Byte(uint8_t& value) {
            value = _position < _size ? _data[_position] : uint8_t{0};
            ++_position;
        }

    private:
        const uint8_t* _data;
        size_t _size;
        size_t _position = 0;
    };

    /// Writes the fields in the form the Reader reads
    class Writer {
    public:
        explicit Writer(Vector<uint8_t>& bytes) : _bytes(bytes) {}

        void Float(float& value) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            for (size_t i = 0; i < sizeof(bits); ++i) {
                _bytes.push_back(static_cast<uint8_t>(bits >> (8 * i)));
            }
        }

        void Byte(uint8_t& value) {
            _bytes.push_back(value);
        }

    private:
        Vector<uint8_t>& _bytes;
    };

    // =============================================================================
    // The fields
    // =============================================================================

    struct SpecCase {
        uint8_t anchor = 0;
        uint8_t optionals = 0;  ///< bits: anchorMin, anchorMax, pivot, minWidth, maxWidth, minHeight, maxHeight
        Vec2 anchorMin;
        Vec2 anchorMax;
        Vec2 pivot;
        UILength x;
        UILength y;
        UILength width;
        UILength height;
        UILength minWidth;
        UILength maxWidth;
        UILength minHeight;
        UILength maxHeight;
        float aspectRatio = 0.0f;
        uint8_t aspectMode = 0;
        UIEdges margin;
        float flex = 0.0f;
        uint8_t unitBits = 0;  ///< bit i: the unit of the i-th length (x, y, width, height, minWidth, maxWidth, minHeight, maxHeight) is a percent

        [[nodiscard]] UIRectSpec ToSpec() const;
    };

    struct StackCase {
        Rect parent;
        uint8_t direction = 0;
        uint8_t align = 0;
        uint8_t wrap = 0;
        float spacing = 0.0f;
        UIEdges padding;
        uint8_t childCount = 0;
        SpecCase children[kMaxStackChildren];
    };

    struct CanvasCase {
        uint8_t space = 0;
        uint8_t scaleMode = 0;
        uint8_t flags = 0;  ///< bits: useSafeArea, extendToOutput
        float referenceWidth = 0.0f;
        float referenceHeight = 0.0f;
        float match = 0.0f;
        float minScale = 0.0f;
        float maxScale = 0.0f;
        UILayoutContext context;
        uint8_t childCount = 0;
        uint8_t grandChildCount = 0;
        uint8_t stackBits = 0;  ///< bit 0: the first child lays its children out in a stack; bit 1: the root's stack settings come from `stack`
        SpecCase children[kMaxCanvasChildren];
        SpecCase grandChildren[kMaxGrandChildren];
        StackCase stack;
        float pointX = 0.0f;
        float pointY = 0.0f;
    };

    struct BoxCase {
        Rect rect;
        UICorners corners;
        float borderWidth = 0.0f;
        Color color;
        Color gradientEnd;
        uint8_t gradientDirection = 0;
        UIShadowSpec shadow;
        float pixelsPerUnit = 1.0f;
        float radiusPixels = 0.0f;
        float inset = 0.0f;
    };

    struct NineSliceCase {
        Rect source;
        UIEdges border;
        Rect destination;
    };

    struct RoundCase {
        BoxCase box;
        NineSliceCase slice;
        Rect parent;
        SpecCase spec;
        StackCase stack;
        CanvasCase canvas;
    };

    // =============================================================================
    // Transfer: one description for the reader and the writer
    // =============================================================================

    template <typename TStream>
    void Transfer(TStream& s, Rect& rect) {
        s.Float(rect.x);
        s.Float(rect.y);
        s.Float(rect.w);
        s.Float(rect.h);
    }

    template <typename TStream>
    void Transfer(TStream& s, UIEdges& edges) {
        s.Float(edges.left);
        s.Float(edges.top);
        s.Float(edges.right);
        s.Float(edges.bottom);
    }

    template <typename TStream>
    void Transfer(TStream& s, Vec2& vector) {
        s.Float(vector.x);
        s.Float(vector.y);
    }

    template <typename TStream>
    void Transfer(TStream& s, Color& color) {
        s.Byte(color.r);
        s.Byte(color.g);
        s.Byte(color.b);
        s.Byte(color.a);
    }

    template <typename TStream>
    void Transfer(TStream& s, SpecCase& spec) {
        s.Byte(spec.anchor);
        s.Byte(spec.optionals);
        s.Byte(spec.unitBits);
        s.Byte(spec.aspectMode);
        Transfer(s, spec.anchorMin);
        Transfer(s, spec.anchorMax);
        Transfer(s, spec.pivot);
        for (UILength* length : {&spec.x, &spec.y, &spec.width, &spec.height, &spec.minWidth, &spec.maxWidth, &spec.minHeight, &spec.maxHeight}) {
            s.Float(length->value);
        }
        s.Float(spec.aspectRatio);
        Transfer(s, spec.margin);
        s.Float(spec.flex);
    }

    template <typename TStream>
    void Transfer(TStream& s, StackCase& stack) {
        Transfer(s, stack.parent);
        s.Byte(stack.direction);
        s.Byte(stack.align);
        s.Byte(stack.wrap);
        s.Byte(stack.childCount);
        s.Float(stack.spacing);
        Transfer(s, stack.padding);
        for (SpecCase& child : stack.children) {
            Transfer(s, child);
        }
    }

    template <typename TStream>
    void Transfer(TStream& s, UILayoutContext& context) {
        s.Float(context.outputWidth);
        s.Float(context.outputHeight);
        s.Float(context.pixelDensity);
        s.Float(context.displayScale);
        Transfer(s, context.safeArea);
        s.Float(context.userScale);
        s.Float(context.sceneViewWidth);
        s.Float(context.sceneViewHeight);
        Transfer(s, context.worldBox);
    }

    template <typename TStream>
    void Transfer(TStream& s, CanvasCase& canvas) {
        s.Byte(canvas.space);
        s.Byte(canvas.scaleMode);
        s.Byte(canvas.flags);
        s.Byte(canvas.childCount);
        s.Byte(canvas.grandChildCount);
        s.Byte(canvas.stackBits);
        s.Float(canvas.referenceWidth);
        s.Float(canvas.referenceHeight);
        s.Float(canvas.match);
        s.Float(canvas.minScale);
        s.Float(canvas.maxScale);
        Transfer(s, canvas.context);
        for (SpecCase& child : canvas.children) {
            Transfer(s, child);
        }
        for (SpecCase& child : canvas.grandChildren) {
            Transfer(s, child);
        }
        Transfer(s, canvas.stack);
        s.Float(canvas.pointX);
        s.Float(canvas.pointY);
    }

    template <typename TStream>
    void Transfer(TStream& s, BoxCase& box) {
        Transfer(s, box.rect);
        s.Float(box.corners.topLeft);
        s.Float(box.corners.topRight);
        s.Float(box.corners.bottomRight);
        s.Float(box.corners.bottomLeft);
        s.Float(box.borderWidth);
        Transfer(s, box.color);
        Transfer(s, box.gradientEnd);
        s.Byte(box.gradientDirection);
        s.Float(box.shadow.offsetX);
        s.Float(box.shadow.offsetY);
        s.Float(box.shadow.blur);
        s.Float(box.shadow.spread);
        Transfer(s, box.shadow.color);
        s.Float(box.pixelsPerUnit);
        s.Float(box.radiusPixels);
        s.Float(box.inset);
    }

    template <typename TStream>
    void Transfer(TStream& s, NineSliceCase& slice) {
        Transfer(s, slice.source);
        Transfer(s, slice.border);
        Transfer(s, slice.destination);
    }

    template <typename TStream>
    void Transfer(TStream& s, RoundCase& round) {
        Transfer(s, round.box);
        Transfer(s, round.slice);
        Transfer(s, round.parent);
        Transfer(s, round.spec);
        Transfer(s, round.stack);
        Transfer(s, round.canvas);
    }

    /// The `index`-th value of an enum, modulo their count: every byte is a valid value
    template <typename TEnum>
    TEnum PickEnum(uint8_t index) {
        const auto& values = EnumUtils<TEnum>::Values();
        return values[index % values.size()];
    }

    inline UIRectSpec SpecCase::ToSpec() const {
        UIRectSpec spec;
        spec.anchor = PickEnum<UIAnchor>(anchor);
        if ((optionals & 0x01) != 0) {
            spec.anchorMin = anchorMin;
        }
        if ((optionals & 0x02) != 0) {
            spec.anchorMax = anchorMax;
        }
        if ((optionals & 0x04) != 0) {
            spec.pivot = pivot;
        }
        const auto length = [this](const UILength& given, unsigned index) { return UILength{given.value, (unitBits >> index & 1u) != 0 ? UIUnit::Percent : UIUnit::Pixels}; };
        spec.x = length(x, 0);
        spec.y = length(y, 1);
        spec.width = length(width, 2);
        spec.height = length(height, 3);
        if ((optionals & 0x08) != 0) {
            spec.minWidth = length(minWidth, 4);
        }
        if ((optionals & 0x10) != 0) {
            spec.maxWidth = length(maxWidth, 5);
        }
        if ((optionals & 0x20) != 0) {
            spec.minHeight = length(minHeight, 6);
        }
        if ((optionals & 0x40) != 0) {
            spec.maxHeight = length(maxHeight, 7);
        }
        spec.aspectRatio = aspectRatio;
        spec.aspectMode = PickEnum<UIAspectMode>(aspectMode);
        spec.margin = margin;
        spec.flex = flex;
        return spec;
    }

}  // namespace BECore::Fuzz::UiGeometryInput
