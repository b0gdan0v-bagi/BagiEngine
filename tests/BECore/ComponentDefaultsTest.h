#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::Tests {

    // Reflected test data of ComponentDefaultsTest; the .gen.hpp is included in its .cpp only

    CORE_ENUM(DefaultsProbeMode, uint8_t, Slow, Fast)

    struct DefaultsProbeInner {
        BE_CLASS(DefaultsProbeInner)

        BE_REFLECT_FIELD int32_t count = 2;
        BE_REFLECT_FIELD String note;
    };

    /// An object a component holds through a pointer
    struct DefaultsProbeItem : RefCounted {
        BE_CLASS(DefaultsProbeItem)

        BE_REFLECT_FIELD int32_t id = 0;
    };

    /// A struct held by value whose list holds pointers
    struct DefaultsProbeHolder {
        BE_CLASS(DefaultsProbeHolder)

        BE_REFLECT_FIELD Vector<IntrusivePtr<DefaultsProbeItem>> items;
    };

    /// A field of each kind the comparison and the reset treat differently
    class DefaultsProbeComponent : public IComponent {
        BE_CLASS(DefaultsProbeComponent)
    public:
        DefaultsProbeComponent() = default;
        ~DefaultsProbeComponent() override = default;

        BE_REFLECT_FIELD float speed = 1.5f;
        BE_REFLECT_FIELD String title{"probe"};
        BE_REFLECT_FIELD PoolString tag;
        BE_REFLECT_FIELD DefaultsProbeMode mode = DefaultsProbeMode::Slow;
        BE_REFLECT_FIELD bool active = true;
        BE_REFLECT_FIELD DefaultsProbeInner inner;
        BE_REFLECT_FIELD Vector<int32_t> values{3};
        BE_REFLECT_FIELD Optional<int32_t> limit;
        BE_REFLECT_FIELD IntrusivePtr<DefaultsProbeItem> item;
        BE_REFLECT_FIELD DefaultsProbeHolder holder;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) int32_t secret = 1;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly) int32_t serial = 1;

        /// The fields `OnPropertyWritten` was called for, and what `speed` held at each call
        Vector<String> written;
        Vector<float> speedAtWrite;

        void OnPropertyWritten(StringView field) override;

        bool HasOnDraw() const override {
            return false;
        }
    };

}  // namespace BECore::Tests
