#include <BECore/PanelBindingProbe.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Generated/PanelBindingProbe.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        PanelBinding MustParse(StringView text) {
            const auto parsed = ParseBinding(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str();
            return parsed.value_or(PanelBinding{});
        }

    }  // namespace

    TEST(PanelBindingTest, ParseSplitsTheSourceFromThePath) {
        const PanelBinding plain = MustParse("files.pending");
        EXPECT_EQ(plain.source, "files");
        EXPECT_EQ(plain.path, "pending");
        EXPECT_FALSE(plain.negated);

        const PanelBinding indexed = MustParse("a.b[2].c");
        EXPECT_EQ(indexed.source, "a");
        EXPECT_EQ(indexed.path, "b[2].c");
    }

    TEST(PanelBindingTest, AQuotedKeyStaysInThePath) {
        const auto parsed = ParseBinding("m.table[\"x y\"].v");
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed->source, "m");
        EXPECT_EQ(parsed->path, "table[\"x y\"].v");
    }

    TEST(PanelBindingTest, ParseRefusesMalformedText) {
        for (const StringView bad : {"", "files", ".path", "files.", "1x.y", "a b.c", "a.b[", "a.[1]x", "!a.b"}) {
            EXPECT_FALSE(ParseBinding(bad).has_value()) << String(bad).c_str();
        }
    }

    TEST(PanelBindingTest, ParseConditionTakesALeadingNegation) {
        const auto negated = ParseCondition("!files.running");
        ASSERT_TRUE(negated.has_value());
        EXPECT_TRUE(negated->negated);
        EXPECT_EQ(negated->source, "files");
        EXPECT_EQ(negated->path, "running");

        const auto plain = ParseCondition("files.running");
        ASSERT_TRUE(plain.has_value());
        EXPECT_FALSE(plain->negated);

        EXPECT_FALSE(ParseCondition("!!a.b").has_value());
        EXPECT_FALSE(ParseCondition("!").has_value());
    }

    TEST(PanelBindingTest, ParseErrorIsOneShortLine) {
        const auto parsed = ParseBinding("files");
        ASSERT_FALSE(parsed.has_value());
        EXPECT_FALSE(parsed.error().empty());
        EXPECT_EQ(parsed.error().find('\n'), String::npos);
    }

    TEST(PanelDataSourcesTest, FindsARegisteredObjectByName) {
        PanelDataSources sources;
        PanelProbe probe;
        probe.count = 7;
        const PanelSourceHandle handle = sources.Register("probe", [&probe] { return TypedRef::Of(probe); });

        const TypedRef found = sources.Find("probe");
        ASSERT_TRUE(found);
        EXPECT_EQ(found.object, &probe);
        EXPECT_TRUE(sources.Contains("probe"));
        EXPECT_FALSE(sources.Find("other"));
        EXPECT_FALSE(sources.Contains("other"));
    }

    TEST(PanelDataSourcesTest, TheProviderIsAskedAtEveryRead) {
        PanelDataSources sources;
        PanelProbe first;
        PanelProbe second;
        PanelProbe* current = &first;
        const PanelSourceHandle handle = sources.Register("game", [&current] { return current != nullptr ? TypedRef::Of(*current) : TypedRef{}; });

        EXPECT_EQ(sources.Find("game").object, &first);
        current = &second;
        EXPECT_EQ(sources.Find("game").object, &second);
        current = nullptr;
        EXPECT_FALSE(sources.Find("game"));
        EXPECT_TRUE(sources.Contains("game"));
    }

    TEST(PanelDataSourcesTest, TheHandleUnregistersOnDestruction) {
        PanelDataSources sources;
        PanelProbe probe;
        {
            const PanelSourceHandle handle = sources.Register("probe", [&probe] { return TypedRef::Of(probe); });
            EXPECT_TRUE(sources.Find("probe"));
        }
        EXPECT_FALSE(sources.Contains("probe"));
    }

    TEST(PanelDataSourcesTest, AMovedHandleKeepsTheRegistration) {
        PanelDataSources sources;
        PanelProbe probe;
        PanelSourceHandle outer;
        {
            PanelSourceHandle inner = sources.Register("probe", [&probe] { return TypedRef::Of(probe); });
            outer = std::move(inner);
            EXPECT_FALSE(inner);
        }
        EXPECT_TRUE(sources.Find("probe"));
        outer.Reset();
        EXPECT_FALSE(sources.Contains("probe"));
    }

    TEST(PanelDataSourcesTest, ARegistrationReplacesTheOldOneAndTheOldHandleRemovesNothing) {
        PanelDataSources sources;
        PanelProbe oldProbe;
        PanelProbe newProbe;
        PanelSourceHandle oldHandle = sources.Register("probe", [&oldProbe] { return TypedRef::Of(oldProbe); });
        PanelSourceHandle newHandle = sources.Register("probe", [&newProbe] { return TypedRef::Of(newProbe); });

        EXPECT_EQ(sources.Find("probe").object, &newProbe);
        oldHandle.Reset();
        EXPECT_EQ(sources.Find("probe").object, &newProbe);
        newHandle.Reset();
        EXPECT_FALSE(sources.Contains("probe"));
    }

    TEST(PanelDataSourcesTest, ALookupNeverInternsTheName) {
        PanelDataSources sources;
        const String fresh = FreshText("panel_source");
        EXPECT_FALSE(sources.Find(fresh));
        EXPECT_FALSE(PoolString::Find(StringView(fresh)).has_value());
    }

}  // namespace BECore::Tests
