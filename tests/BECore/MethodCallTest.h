#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::MethodCallData {

    // =========================================================================
    // Reflected test data of CallMethod (MethodCallTest). The .gen.hpp is generated from this header and included in MethodCallTest.cpp
    // only. The names are unique among all test headers: a type is found by its simple name.
    // =========================================================================

    CORE_ENUM(McMode, uint8_t, Off, Slow, Fast)

    struct McPoint {
        BE_CLASS(McPoint)

        BE_REFLECT_FIELD int32_t x = 0;
        BE_REFLECT_FIELD int32_t y = 0;
    };

    /// A base with virtual methods: the override of the derived class must run through the base's own entry
    class McBase : public RefCounted {
        BE_CLASS(McBase)

    public:
        McBase() = default;
        ~McBase() override = default;

        BE_REFLECT_FIELD int32_t baseValue = 1;

        BE_FUNCTION int32_t Bump(int32_t by) {
            baseValue += by;
            return baseValue;
        }

        BE_FUNCTION virtual int32_t Twice(int32_t value) {
            return value * 2;
        }

        BE_FUNCTION virtual int32_t Speed() const {
            return 10;
        }
    };

    class McDerived : public McBase {
        BE_CLASS(McDerived)

    public:
        McDerived() = default;
        ~McDerived() override = default;

        BE_REFLECT_FIELD int32_t ownValue = 2;

        /// Overrides without a mark: the entry of the base calls it
        int32_t Twice(int32_t value) override {
            return value * 3;
        }

        /// Overrides with a mark of its own: found first from a derived object
        BE_FUNCTION int32_t Speed() const override {
            return 20;
        }

        BE_FUNCTION int32_t Own() const {
            return ownValue;
        }
    };

    /// Not reflected: it sits before the reflected base of `McShifted`, so that the base subobject is not at offset 0
    struct McPad {
        virtual ~McPad() = default;

        int64_t padding[2]{};
    };

    class McShifted : public McPad, public McBase {
        BE_CLASS(McShifted)

    public:
        McShifted() = default;
        ~McShifted() override = default;

        BE_REFLECT_FIELD int32_t shiftedValue = 3;

        int32_t Twice(int32_t value) override {
            return value * 5;
        }
    };

    /// One method per kind of parameter and of result, and the methods that cannot be called by name
    struct McCounter {
        BE_CLASS(McCounter)

        BE_REFLECT_FIELD int32_t total = 0;
        BE_REFLECT_FIELD String log;

        BE_FUNCTION int32_t Add(int32_t amount, const String& label) {
            total += amount + static_cast<int32_t>(label.size());
            log = label;
            return total;
        }

        BE_FUNCTION void Reset() {
            total = 0;
            log.clear();
        }

        BE_FUNCTION bool IsEmpty() const {
            return total == 0;
        }

        BE_FUNCTION int64_t Widths(int8_t a, uint8_t b, int16_t c, uint16_t d, uint32_t e, int64_t f, uint64_t g) {
            total = a;
            return static_cast<int64_t>(a + b + c + d) + static_cast<int64_t>(e) + f + static_cast<int64_t>(g);
        }

        BE_FUNCTION double Scale(float factor, double offset) {
            total += 1;
            return static_cast<double>(factor) * 2.0 + offset;
        }

        BE_FUNCTION McMode Next(McMode mode, bool flag) {
            total += 1;
            return flag ? mode : McMode::Off;
        }

        BE_FUNCTION int32_t Sum(const Vector<int32_t>& values) {
            for (const int32_t value : values) {
                total += value;
            }
            return total;
        }

        BE_FUNCTION Optional<int32_t> Pick(Optional<int32_t> index) {
            total += 1;
            return index;
        }

        BE_FUNCTION McPoint Shift(const McPoint& by, McPoint extra) {
            total += 1;
            McPoint sum;
            sum.x = by.x + extra.x;
            sum.y = by.y + extra.y;
            return sum;
        }

        BE_FUNCTION Vector<int32_t> Range(int32_t count) {
            total += 1;
            Vector<int32_t> values;
            for (int32_t i = 0; i < count && i < 8; ++i) {
                values.push_back(i);
            }
            return values;
        }

        BE_FUNCTION String Describe() const {
            return log;
        }

        BE_FUNCTION int32_t Multiply(int32_t, int32_t) {
            total += 1;
            return total;
        }

        BE_FUNCTION bool Initialize(IDeserializer& deserializer);

        BE_FUNCTION void Fill(Vector<int32_t>& out);

        BE_FUNCTION McCounter* Self();

        /// An object cannot be made from a call's arguments nor handed back through one, so a pointer anywhere in the signature is not callable
        BE_FUNCTION void Adopt(IntrusivePtr<McBase> object);

        BE_FUNCTION Vector<IntrusivePtr<McBase>> Held() const;
    };

}  // namespace BECore::MethodCallData
