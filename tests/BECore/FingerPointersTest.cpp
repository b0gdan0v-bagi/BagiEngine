#include <BECore/Input/FingerPointers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr SDL_FingerID kFirst = 1001;
        constexpr SDL_FingerID kSecond = 1002;
        constexpr SDL_FingerID kThird = 1003;
        constexpr SDL_FingerID kUnknown = 9999;

    }  // namespace

    TEST(FingerPointersTest, EachFingerGetsAPointerIdOfItsOwnInTheFingerRange) {
        FingerPointers fingers;

        const Optional<uint32_t> first = fingers.Begin(kFirst);
        const Optional<uint32_t> second = fingers.Begin(kSecond);

        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        EXPECT_NE(*first, *second);
        EXPECT_TRUE(IsFingerPointer(*first));
        EXPECT_TRUE(IsFingerPointer(*second));
        EXPECT_TRUE(IsTouchPointer(*first));
        EXPECT_EQ(fingers.Count(), 2u);
    }

    TEST(FingerPointersTest, FindAnswersTheIdOfAFingerThatIsDown) {
        FingerPointers fingers;
        const Optional<uint32_t> first = fingers.Begin(kFirst);

        EXPECT_EQ(fingers.Find(kFirst), first);
        EXPECT_FALSE(fingers.Find(kUnknown));
    }

    TEST(FingerPointersTest, ADownOfAKnownFingerKeepsItsPointerId) {
        FingerPointers fingers;
        const Optional<uint32_t> first = fingers.Begin(kFirst);

        EXPECT_EQ(fingers.Begin(kFirst), first);
        EXPECT_EQ(fingers.Count(), 1u);
    }

    TEST(FingerPointersTest, APlaceIsReusedOnceItsFingerIsUp) {
        FingerPointers fingers;
        const Optional<uint32_t> first = fingers.Begin(kFirst);
        const Optional<uint32_t> second = fingers.Begin(kSecond);

        EXPECT_EQ(fingers.End(kFirst), first);
        EXPECT_FALSE(fingers.Find(kFirst));
        EXPECT_EQ(fingers.Find(kSecond), second);

        EXPECT_EQ(fingers.Begin(kThird), first) << "the lowest free place";
    }

    TEST(FingerPointersTest, ANinthFingerIsRefusedAndTheOthersKeepTheirIds) {
        FingerPointers fingers;
        for (uint32_t i = 0; i < kMaxFingerPointers; ++i) {
            ASSERT_TRUE(fingers.Begin(kFirst + i)) << i;
        }
        const Optional<uint32_t> last = fingers.Find(kFirst + kMaxFingerPointers - 1);

        EXPECT_FALSE(fingers.Begin(kUnknown));
        EXPECT_EQ(fingers.Count(), kMaxFingerPointers);
        EXPECT_EQ(fingers.Find(kFirst + kMaxFingerPointers - 1), last);

        fingers.End(kFirst);
        EXPECT_TRUE(fingers.Begin(kUnknown)) << "a place freed by an up takes the next finger";
    }

    TEST(FingerPointersTest, EndOfAnUnknownFingerChangesNothing) {
        FingerPointers fingers;
        fingers.Begin(kFirst);

        EXPECT_FALSE(fingers.End(kUnknown));
        EXPECT_EQ(fingers.Count(), 1u);
    }

}  // namespace BECore::Tests
