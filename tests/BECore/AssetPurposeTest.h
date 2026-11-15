#pragma once

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/TypeTraits.h>

namespace BECore::Tests {

    namespace AssetPurposeTestTypes {

        /// One reflected type of each purpose; shared by the purpose, filter and asset part tests.
        struct PurposeSimProbe {
            BE_CLASS(PurposeSimProbe, PURPOSE = sim)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct PurposeVisualProbe {
            BE_CLASS(PurposeVisualProbe, purpose = Visual)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct PurposeEditorProbe {
            BE_CLASS(PurposeEditorProbe, PURPOSE = editor)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct PurposeCommonProbe {
            BE_CLASS(PurposeCommonProbe)

            BE_REFLECT_FIELD int32_t value = 0;
        };

    }  // namespace AssetPurposeTestTypes

}  // namespace BECore::Tests
