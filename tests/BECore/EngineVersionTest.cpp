#include <BECore/Format/Format.h>
#include <BECore/Utils/EngineVersion.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    TEST(EngineVersionTest, MacrosAreUsableInPreprocessorConditions) {
#if BE_ENGINE_VERSION_MAJOR >= 0 && BE_ENGINE_VERSION_AT_LEAST(0, 0, 0)
        SUCCEED();
#else
        FAIL() << "BE_ENGINE_VERSION_* is not usable in #if";
#endif
    }

    TEST(EngineVersionTest, AtLeastIsTrueForTheCurrentAndLowerVersionsOnly) {
        constexpr int major = BE_ENGINE_VERSION_MAJOR;
        constexpr int minor = BE_ENGINE_VERSION_MINOR;
        constexpr int patch = BE_ENGINE_VERSION_PATCH;

        static_assert(BE_ENGINE_VERSION_AT_LEAST(major, minor, patch));
        static_assert(BE_ENGINE_VERSION_AT_LEAST(0, 0, 0));
        static_assert(!BE_ENGINE_VERSION_AT_LEAST(major, minor, patch + 1));
        static_assert(!BE_ENGINE_VERSION_AT_LEAST(major, minor + 1, 0));
        static_assert(!BE_ENGINE_VERSION_AT_LEAST(major + 1, 0, 0));
        // A lower MINOR is enough whatever the PATCH.
        static_assert(BE_ENGINE_VERSION_AT_LEAST(major, minor - 1, patch + 100));
        SUCCEED();
    }

    TEST(EngineVersionTest, StringAgreesWithTheNumbers) {
        static_assert(kEngineVersionMajor == BE_ENGINE_VERSION_MAJOR);
        static_assert(kEngineVersionMinor == BE_ENGINE_VERSION_MINOR);
        static_assert(kEngineVersionPatch == BE_ENGINE_VERSION_PATCH);

        EXPECT_EQ(String(kEngineVersion), Format("{}.{}.{}", kEngineVersionMajor, kEngineVersionMinor, kEngineVersionPatch));
        EXPECT_EQ(StringView(BE_ENGINE_VERSION_STRING), kEngineVersion);
    }

}  // namespace BECore::Tests
