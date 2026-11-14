#include <Common/Printers.h>
#include <Fuzz/FuzzMutator.h>
#include <set>

namespace BECore::Tests {

    using Fuzz::ApplyMutation;
    using Fuzz::FuzzBytes;
    using Fuzz::FuzzMutation;
    using Fuzz::FuzzRandom;
    using Fuzz::Mutate;

    namespace {

        using MutationUtils = EnumUtils<FuzzMutation>;

        constexpr size_t kInputSize = 64;

        /// A byte that none of the "interesting" values equals, so a SetByte / SetWord always changes the input
        constexpr uint8_t kPlainByte = 0x55;

        constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ull;
        constexpr uint64_t kFnvPrime = 0x100000001B3ull;

        FuzzBytes PlainInput(size_t size = kInputSize) {
            return FuzzBytes(size, kPlainByte);
        }

        FuzzBytes OtherSeed() {
            FuzzBytes seed;
            for (uint8_t value = 1; value <= 40; ++value) {
                seed.push_back(value);
            }
            return seed;
        }

        uint64_t Hash(const FuzzBytes& bytes, uint64_t state = kFnvOffset) {
            for (const uint8_t byte : bytes) {
                state = (state ^ byte) * kFnvPrime;
            }
            return state;
        }

        FuzzMutation KindAt(size_t index) {
            return static_cast<FuzzMutation>(index);
        }

        std::string Name(FuzzMutation kind) {
            const StringView name = MutationUtils::ToString(kind);
            return std::string(name.data(), name.size());
        }

    }  // namespace

    TEST(FuzzMutatorTest, RandomIsXorshift64Star) {
        // Reference values of xorshift64* with the state 1, computed outside the engine
        FuzzRandom random(1);
        EXPECT_EQ(random.Next(), 0x47e4ce4b896cdd1dull);
        EXPECT_EQ(random.Next(), 0xabcfa6a8e079651dull);
        EXPECT_EQ(random.Next(), 0xb9d10d8feb731f57ull);
        EXPECT_EQ(random.Next(), 0x4db418a0bb1b019dull);
    }

    TEST(FuzzMutatorTest, ZeroSeedDoesNotStickAtZero) {
        FuzzRandom random(0);
        EXPECT_NE(random.Next(), 0u);
        EXPECT_NE(random.Next(), 0u);
    }

    TEST(FuzzMutatorTest, BelowStaysInTheBound) {
        FuzzRandom random(7);
        for (int i = 0; i < 1000; ++i) {
            EXPECT_LT(random.Below(5), 5u);
        }
        EXPECT_EQ(random.Below(0), 0u);
        EXPECT_EQ(random.Below(1), 0u);
    }

    TEST(FuzzMutatorTest, SameSeedGivesTheSameBytes) {
        const std::vector<FuzzBytes> others = {OtherSeed()};

        const auto run = [&] {
            FuzzRandom random(2024);
            FuzzBytes bytes = PlainInput();
            uint64_t hash = kFnvOffset;
            for (int i = 0; i < 200; ++i) {
                Mutate(bytes, random, others);
                hash = Hash(bytes, hash);
            }
            return hash;
        };

        EXPECT_EQ(run(), run());
        // Pinned: a change of the generator or of a mutation changes every corpus run, so it is a decision, not an accident
        EXPECT_EQ(run(), 0xcdae3993457bd7f8ull);
    }

    TEST(FuzzMutatorTest, EveryKindIsReachable) {
        const std::vector<FuzzBytes> others = {OtherSeed()};
        FuzzRandom random(99);
        Set<uint8_t> seen;
        for (int i = 0; i < 400; ++i) {
            FuzzBytes bytes = PlainInput();
            seen.insert(static_cast<uint8_t>(Mutate(bytes, random, others)));
        }
        for (size_t kind = 0; kind < MutationUtils::Count(); ++kind) {
            EXPECT_TRUE(seen.contains(static_cast<uint8_t>(kind))) << Name(KindAt(kind));
        }
    }

    TEST(FuzzMutatorTest, EachKindDoesWhatItsNameSays) {
        const std::vector<FuzzBytes> others = {OtherSeed()};
        for (size_t index = 0; index < MutationUtils::Count(); ++index) {
            const FuzzMutation kind = KindAt(index);
            FuzzRandom random(5);
            const FuzzBytes original = PlainInput();
            FuzzBytes bytes = original;

            EXPECT_EQ(ApplyMutation(kind, bytes, random, others), kind) << Name(kind);
            EXPECT_NE(bytes, original) << Name(kind);

            switch (kind) {
                case FuzzMutation::FlipBit:
                case FuzzMutation::SetByte:
                case FuzzMutation::SetWord:
                    EXPECT_EQ(bytes.size(), original.size());
                    break;
                case FuzzMutation::InsertBlock:
                case FuzzMutation::DuplicateBlock:
                case FuzzMutation::Splice:
                    EXPECT_GT(bytes.size(), original.size());
                    break;
                case FuzzMutation::DeleteBlock:
                case FuzzMutation::Truncate:
                    EXPECT_LT(bytes.size(), original.size());
                    break;
            }
        }
    }

    TEST(FuzzMutatorTest, SetWordWritesALengthLikeValue) {
        FuzzRandom random(11);
        Set<uint32_t> seen;
        for (int i = 0; i < 200; ++i) {
            FuzzBytes bytes(8, 0x55);
            ApplyMutation(FuzzMutation::SetWord, bytes, random, {});
            for (size_t offset = 0; offset + sizeof(uint32_t) <= bytes.size(); ++offset) {
                uint32_t word = 0;
                std::memcpy(&word, bytes.data() + offset, sizeof(word));
                if (word == 0xFFFFFFFFu || word == 0x7FFFFFFFu || word == 0x80000000u || word == 0u) {
                    seen.insert(word);
                }
            }
        }
        EXPECT_EQ(seen.size(), 4u);
    }

    TEST(FuzzMutatorTest, EmptyInputBecomesAnInsert) {
        for (size_t index = 0; index < MutationUtils::Count(); ++index) {
            FuzzRandom random(3);
            FuzzBytes bytes;
            EXPECT_EQ(ApplyMutation(KindAt(index), bytes, random, {}), FuzzMutation::InsertBlock) << Name(KindAt(index));
            EXPECT_FALSE(bytes.empty());
        }
    }

    TEST(FuzzMutatorTest, KindsFallBackToTheNearestOneThatFits) {
        FuzzRandom random(3);

        FuzzBytes shortInput(3, kPlainByte);
        EXPECT_EQ(ApplyMutation(FuzzMutation::SetWord, shortInput, random, {}), FuzzMutation::SetByte);

        FuzzBytes input = PlainInput();
        EXPECT_EQ(ApplyMutation(FuzzMutation::Splice, input, random, {}), FuzzMutation::DuplicateBlock);

        const std::vector<FuzzBytes> emptySeed = {FuzzBytes()};
        EXPECT_EQ(ApplyMutation(FuzzMutation::Splice, input, random, emptySeed), FuzzMutation::DuplicateBlock);
    }

    TEST(FuzzMutatorTest, ResultNeverPassesTheCap) {
        const std::vector<FuzzBytes> others = {PlainInput(Fuzz::kMaxFuzzInputSize)};
        FuzzRandom random(17);
        FuzzBytes bytes = PlainInput(Fuzz::kMaxFuzzInputSize);
        for (int i = 0; i < 2000; ++i) {
            Mutate(bytes, random, others);
            ASSERT_LE(bytes.size(), Fuzz::kMaxFuzzInputSize);
        }
        for (size_t index = 0; index < MutationUtils::Count(); ++index) {
            FuzzBytes full = PlainInput(Fuzz::kMaxFuzzInputSize);
            ApplyMutation(KindAt(index), full, random, others);
            EXPECT_LE(full.size(), Fuzz::kMaxFuzzInputSize) << Name(KindAt(index));
        }
    }

}  // namespace BECore::Tests
