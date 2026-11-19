#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore::Tests {

    // =========================================================================
    // Reflected test data for the load diagnostics. The .gen.hpp is generated from this header and included in
    // XmlDiagnosticsTest.cpp only.
    // =========================================================================

    namespace XmlDiagnosticsTestData {

        CORE_ENUM(ProbeKind, uint8_t, Small, Large)

        /// A reflected object read as a child element
        struct Leaf {
            BE_CLASS(Leaf)

            BE_REFLECT_FIELD int32_t leafValue = 0;
        };

        /// An enum field and an object field
        struct Shapes {
            BE_CLASS(Shapes)

            BE_REFLECT_FIELD ProbeKind kind = ProbeKind::Small;
            BE_REFLECT_FIELD Leaf leaf;
        };

        /// One field of each kind the `required` and `range` checks look at
        struct Constrained {
            BE_CLASS(Constrained)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(required) int32_t needed = 0;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1)) float ratio = 0.5f;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(1, 10)) int32_t count = 5;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(required, range(0, 100)) int32_t both = 50;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(required) Leaf requiredLeaf;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(required) Vector<Leaf> requiredLeaves;
            BE_REFLECT_FIELD int32_t free = 0;
        };

        struct ConstrainedChild : Constrained {
            BE_CLASS(ConstrainedChild)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 3)) int32_t own = 0;
        };

        /// `range` on containers: it reaches their numeric elements, the mapped values, an engaged optional and a variant's alternative
        struct RangedContainers {
            BE_CLASS(RangedContainers)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1)) Vector<float> ratios;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(1, 10)) Array<int32_t, 3> counts{};
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1)) Optional<float> maybe;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1)) Map<String, float> weights;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(1, 10)) Variant<int32_t, String> choice;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1)) Vector<Leaf> leaves;
        };

        /// Two names a typo can be mistaken for each other
        struct TileConfig {
            BE_CLASS(TileConfig)

            BE_REFLECT_FIELD int32_t tileSize = 0;
            BE_REFLECT_FIELD int32_t tileCount = 0;
        };

        /// Factory base whose element is `probe`
        class DiagProbe : public RefCounted {
            BE_CLASS(DiagProbe, FACTORY_BASE, ELEMENT = probe)
        public:
            DiagProbe() = default;
            ~DiagProbe() override = default;

            virtual bool Initialize(IDeserializer& /*deserializer*/) {
                return true;
            }
        };

        /// Reads one more attribute of its own element in Initialize(IDeserializer&)
        class DiagProbeReads : public DiagProbe {
            BE_CLASS(DiagProbeReads)
        public:
            DiagProbeReads() = default;
            ~DiagProbeReads() override = default;

            bool Initialize(IDeserializer& deserializer) override {
                deserializer.ReadAttribute("extra", extra);
                return true;
            }

            BE_REFLECT_FIELD int32_t id = 0;
            int32_t extra = 0;
        };

        /// One field of each shape that opens a scope: an object, a list of objects, a map, a factory list
        struct Holder {
            BE_CLASS(Holder)

            BE_REFLECT_FIELD Leaf leaf;
            BE_REFLECT_FIELD Vector<Leaf> leaves;
            BE_REFLECT_FIELD HashMap<int32_t, Leaf> table;
            BE_REFLECT_FIELD Vector<IntrusivePtrNonAtomic<DiagProbe>> probes;
        };

    }  // namespace XmlDiagnosticsTestData

}  // namespace BECore::Tests
