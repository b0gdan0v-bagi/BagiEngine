#include <BECore/Profiler/Alloc/AllocRouting.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSize = 256;

    }  // namespace

    TEST(AllocRoutingTest, MallocGivesWritableMemory) {
        auto* bytes = static_cast<unsigned char*>(AllocRouting::Malloc(AllocSource::Stb, kSize));
        ASSERT_NE(bytes, nullptr);
        std::memset(bytes, 0xAB, kSize);
        EXPECT_EQ(bytes[kSize - 1], 0xAB);
        AllocRouting::Free(AllocSource::Stb, bytes);
    }

    TEST(AllocRoutingTest, CallocGivesZeroedMemory) {
        auto* bytes = static_cast<unsigned char*>(AllocRouting::Calloc(AllocSource::DrLibs, kSize, 4));
        ASSERT_NE(bytes, nullptr);
        for (size_t i = 0; i < kSize * 4; ++i) {
            ASSERT_EQ(bytes[i], 0) << "byte " << i;
        }
        AllocRouting::Free(AllocSource::DrLibs, bytes);
    }

    TEST(AllocRoutingTest, ReallocKeepsTheContentWhenTheBlockGrows) {
        auto* bytes = static_cast<unsigned char*>(AllocRouting::Malloc(AllocSource::Stb, kSize));
        ASSERT_NE(bytes, nullptr);
        std::memset(bytes, 0x5C, kSize);
        bytes = static_cast<unsigned char*>(AllocRouting::Realloc(AllocSource::Stb, bytes, kSize * 64));
        ASSERT_NE(bytes, nullptr);
        EXPECT_EQ(bytes[0], 0x5C);
        EXPECT_EQ(bytes[kSize - 1], 0x5C);
        AllocRouting::Free(AllocSource::Stb, bytes);
    }

    TEST(AllocRoutingTest, ReallocOfNullptrAndFreeOfNullptrAreAccepted) {
        void* bytes = AllocRouting::Realloc(AllocSource::Stb, nullptr, kSize);
        ASSERT_NE(bytes, nullptr);
        AllocRouting::Free(AllocSource::Stb, bytes);
        AllocRouting::Free(AllocSource::Stb, nullptr);
    }

}  // namespace BECore::Tests
