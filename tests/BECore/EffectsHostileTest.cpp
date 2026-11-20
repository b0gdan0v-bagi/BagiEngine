// Hostile text of a script (Values/Effects/CLAUDE.md, "Compiling"): a good script with a byte changed, cut, deleted or repeated either compiles and runs, or is an error whose column is
// inside the text and that adds nothing to the pool of programs. A fixed seed, so a failure repeats.

#include <BECore/EffectsTriggerTypes.h>
#include <BECore/Random/Rng.h>
#include <BECore/Values/Effects/EffectCompiler.h>
#include <BECore/Values/Effects/ScriptAst.h>
#include <BECore/Values/Effects/WriteTargets.h>
#include <BECore/Values/Programs/Program.h>
#include <Common/AssertBridge.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace EffectsData;

    namespace {

        constexpr uint64_t kSeed = 0xBADC0DE;
        constexpr uint32_t kCases = 4000;
        constexpr size_t kLongestFragment = 40;
        constexpr size_t kMostRepeats = 120;
        constexpr uint32_t kMutationKinds = 6;
        constexpr size_t kBufferCapacity = 4;
        constexpr StringView kInteresting = "(){};=+-,.!<>&|0123456789 abcxyz'\"\n\t";

        struct HostileWorld {
            ContextDecl declaration;
            TriggerCell cell;
            TriggerBoard board;
            Context context{declaration};
            WriteTargets targets;
            EffectOptions options;

            HostileWorld() {
                static_cast<void>(RegisterTriggerAction());
                EXPECT_TRUE(declaration.Add<TriggerCell>("cell").has_value());
                EXPECT_TRUE(declaration.Add<TriggerBoard>("board").has_value());
                context.SetMutable(0, cell);
                context.Set(1, board);
                EXPECT_TRUE(targets.Allow("cell"));
                options.targets = &targets;
            }

            HostileWorld(const HostileWorld&) = delete;
            HostileWorld& operator=(const HostileWorld&) = delete;
        };

        String Repeated(StringView part, size_t count) {
            String text;
            for (size_t i = 0; i < count; ++i) {
                text.append(part.data(), part.size());
            }
            return text;
        }

        /// The good text with one change of the kind @p kind at a place @p rng picks
        String Mutated(StringView good, uint32_t kind, Rng& rng) {
            String text(good.data(), good.size());
            const size_t at = rng.Next32() % text.size();
            switch (kind) {
                case 0:  // any byte, the zero and the high ones too
                    text[at] = static_cast<char>(rng.Next32() & 0xFFU);
                    break;
                case 1:  // a byte of the grammar
                    text[at] = kInteresting[rng.Next32() % kInteresting.size()];
                    break;
                case 2:
                    text.resize(at);
                    break;
                case 3: {
                    const size_t count = 1 + rng.Next32() % kLongestFragment;
                    text.erase(at, count);
                    break;
                }
                case 4: {
                    const size_t length = 1 + rng.Next32() % kLongestFragment;
                    const String fragment(text.substr(at, length));
                    text.insert(at, Repeated(fragment, 2 + rng.Next32() % kMostRepeats));
                    break;
                }
                default:
                    text = Repeated(text, 2 + rng.Next32() % kMostRepeats);
                    break;
            }
            return text;
        }

        void ExpectCompilesOrRefusesInsideTheText(HostileWorld& world, StringView text, EmitBuffer& buffer) {
            const size_t pooled = ProgramPool::Count();
            const auto compiled = CompileEffects(text, world.declaration, world.options);
            if (!compiled.has_value()) {
                const ProgramError& error = compiled.error();
                EXPECT_FALSE(error.message.empty());
                EXPECT_GE(error.column, 1U) << error.message.c_str();
                EXPECT_LE(error.column, text.size() + 1) << error.message.c_str();
                EXPECT_EQ(ProgramPool::Count(), pooled) << error.message.c_str();
                return;
            }
            buffer.Clear();
            const EffectResult result = compiled->Run(world.context, buffer);
            EXPECT_EQ(result.refused, 0U);
        }

    }  // namespace

    TEST(EffectsHostileTest, ChangedCutAndRepeatedScriptsCompileOrAreErrorsInsideTheTextAndAddNothingToThePool) {
        HostileWorld world;
        const String good = Format("{} cell.tone = Loud; toggle cell.open; cell.timer++;", kTriggerScript);
        EmitBufferStorage<kBufferCapacity> storage;
        ASSERT_TRUE(CompileEffects(good, world.declaration, world.options).has_value());
        Rng rng(kSeed);
        uint32_t compiled = 0;
        for (uint32_t i = 0; i < kCases; ++i) {
            const String text = Mutated(good, i % kMutationKinds, rng);
            const size_t before = ProgramPool::Count();
            ExpectCompilesOrRefusesInsideTheText(world, text, storage.Buffer());
            compiled += ProgramPool::Count() != before ? 1U : 0U;
            if (::testing::Test::HasFailure()) {
                FAIL() << "case " << i << " of kind " << i % kMutationKinds << ", " << text.size() << " bytes: " << String(text.substr(0, 200)).c_str();
            }
        }
        // The generator reaches both outcomes, so neither check above is idle
        EXPECT_GT(compiled, 0U);
        EXPECT_LT(compiled, kCases);
    }

    TEST(EffectsHostileTest, TextsOverTheLimitsAndDegenerateTextsAreErrorsThatNameWhatIsToBlame) {
        HostileWorld world;
        struct Case {
            String text;
            StringView words;
        };
        const Case cases[] = {
            {Repeated("cell.timer = 1; ", ScriptLimits::kMaxStatements + 1), "statement"},
            {Repeated("if (true) { ", ScriptLimits::kMaxBlockDepth + 1) + "cell.timer = 1;" + Repeated(" }", ScriptLimits::kMaxBlockDepth + 1), "nested deeper"},
            {Repeated(" ", ScriptLimits::kMaxTextBytes + 1), "bytes"},
            {String(100000, 'x'), "bytes"},
            {Repeated("(", 5000), ""},
            {Repeated("{", 5000), ""},
            {Repeated("if (cell.open) ", 3000), ""},
            {Repeated("else ", 3000), ""},
            {Repeated(";", 3000), ""},
            {String(1, '\0'), ""},
            {String("cell.timer = 1\0; cell.timer = 2;", 33), ""},
        };
        for (const Case& hostile : cases) {
            const size_t pooled = ProgramPool::Count();
            const auto compiled = CompileEffects(hostile.text, world.declaration, world.options);
            // An empty script of only `;` is the one that may compile: it has no statements
            if (compiled.has_value()) {
                EXPECT_TRUE(compiled->Empty() || hostile.text.find(';') != String::npos);
                continue;
            }
            EXPECT_TRUE(Contains(compiled.error().message, hostile.words)) << compiled.error().message.c_str();
            EXPECT_LE(compiled.error().column, hostile.text.size() + 1);
            EXPECT_EQ(ProgramPool::Count(), pooled);
        }
    }

}  // namespace BECore::Tests
