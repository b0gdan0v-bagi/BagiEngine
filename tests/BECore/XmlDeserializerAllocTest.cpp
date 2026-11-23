// XmlDeserializer: the Warn mode costs nothing per node over the Off mode (its scopes' lists live in an arena the deserializer owns).

#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Past ReaderLimits::kMaxRecordedErrors, so the findings stop growing and only the walk is left to measure
        constexpr size_t kManyGroups = 1000;
        constexpr size_t kFewGroups = 100;
        constexpr size_t kItemsPerGroup = 3;

        /// What the Warn mode may allocate beyond the Off mode in a second load by the same deserializer: a few blocks, never one per node
        constexpr uint64_t kLoadSlack = 8;

        String MakeDocument(size_t groups, bool withUnknownKeys) {
            String text = "<root>\n<groups>\n";
            for (size_t i = 0; i < groups; ++i) {
                AppendFormat(text, "<group id=\"{}\" description=\"a text longer than the small string\"{}>\n", i, withUnknownKeys ? " extraAttribute=\"1\"" : "");
                text += "<child weight=\"2\"/>\n";
                text += "<items>\n";
                for (size_t item = 0; item < kItemsPerGroup; ++item) {
                    AppendFormat(text, "<item value=\"{}\"/>\n", item);
                }
                text += "</items>\n";
                if (withUnknownKeys) {
                    text += "<extraElement/>\n";
                }
                text += "</group>\n";
            }
            text += "</groups>\n</root>";
            return text;
        }

        /// What a Deserialize of a struct does by hand: attributes, a child object, an array of elements
        size_t LoadGroups(XmlDeserializer& reader) {
            size_t sum = 0;
            size_t groupCount = 0;
            String description;
            if (reader.BeginArray("groups", "group", groupCount)) {
                for (size_t i = 0; i < groupCount; ++i) {
                    if (!reader.BeginArrayElement()) {
                        continue;
                    }
                    uint32_t id = 0;
                    reader.ReadAttribute("id", id);
                    reader.ReadAttribute("description", description);
                    sum += id + description.size();
                    if (reader.BeginObject("child")) {
                        uint32_t weight = 0;
                        reader.ReadAttribute("weight", weight);
                        sum += weight;
                        reader.EndObject();
                    }
                    size_t itemCount = 0;
                    if (reader.BeginArray("items", "item", itemCount)) {
                        for (size_t item = 0; item < itemCount; ++item) {
                            if (reader.BeginArrayElement()) {
                                uint32_t value = 0;
                                reader.ReadAttribute("value", value);
                                sum += value;
                                reader.EndArrayElement();
                            }
                        }
                        reader.EndArray();
                    }
                    reader.EndArrayElement();
                }
                reader.EndArray();
            }
            reader.FinishLoad();
            return sum;
        }

        String Warnings(const IDeserializer& reader) {
            String text;
            for (const DeserializeError& warning : reader.GetWarnings()) {
                AppendFormat(text, "{}: {}\n", warning.path, warning.errorMessage);
            }
            return text;
        }

        /// The allocations of the second load of @p document by one deserializer in @p mode
        AllocCount SecondLoadAllocations(const String& document, DiagnosticsMode mode) {
            XmlDeserializer reader;
            reader.SetDiagnostics(mode);
            EXPECT_TRUE(reader.LoadFromString(document));
            LoadGroups(reader);
            reader.ClearWarnings();
            EXPECT_TRUE(reader.LoadFromString(document));
            return CountAllocations([&] { LoadGroups(reader); });
        }

        /// What the Warn mode costs over the Off mode for @p document
        uint64_t WarnExtra(const String& document) {
            const uint64_t warn = SecondLoadAllocations(document, DiagnosticsMode::Warn).count;
            const uint64_t off = SecondLoadAllocations(document, DiagnosticsMode::Off).count;
            return warn > off ? warn - off : 0;
        }

    }  // namespace

    TEST(XmlDeserializerAllocTest, ACleanLoadInWarnModeAllocatesNoMoreThanOffPerNode) {
        const uint64_t extra = WarnExtra(MakeDocument(kManyGroups, false));
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(extra, kLoadSlack);
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST(XmlDeserializerAllocTest, UnknownKeysCostNoMoreForABiggerDocument) {
        const uint64_t few = WarnExtra(MakeDocument(kFewGroups, true));
        const uint64_t many = WarnExtra(MakeDocument(kManyGroups, true));
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(many, few + kLoadSlack);
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST(XmlDeserializerAllocTest, TheFindingsOfANestedLoadAreTheOnesTheScopesAskedFor) {
        XmlDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(reader.LoadFromString(R"(<root><outer colour="1" shade="2" bogus="3"><inner brightness="4" brightnes="5"/><leftover/></outer></root>)"));
        uint32_t value = 0;
        ASSERT_TRUE(reader.BeginObject("outer"));
        reader.ReadAttribute("colour", value);
        ASSERT_TRUE(reader.BeginObject("inner"));
        reader.ReadAttribute("brightness", value);
        reader.EndObject();
        // Asked after the child closed: its memory went back with the child's scope, the parent's list is intact
        reader.ReadAttribute("shade", value);
        reader.EndObject();
        reader.FinishLoad();

        const String warnings = Warnings(reader);
        EXPECT_NE(warnings.find("outer.bogus: Unknown attribute 'bogus'"), String::npos) << warnings.c_str();
        EXPECT_NE(warnings.find("outer.inner.brightnes: Unknown attribute 'brightnes', did you mean 'brightness'?"), String::npos) << warnings.c_str();
        EXPECT_NE(warnings.find("outer.leftover: Unknown element 'leftover'"), String::npos) << warnings.c_str();
        EXPECT_EQ(reader.GetWarnings().size(), 3u) << warnings.c_str();
    }

    TEST(XmlDeserializerAllocTest, AskedNamesOutliveTheTemporariesThatSpelledThem) {
        XmlDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(reader.LoadFromString(R"(<root><item opacity="1" opacitty="2"/></root>)"));
        ASSERT_TRUE(reader.BeginObject("item"));
        {
            String temporary("opacity");
            uint32_t value = 0;
            reader.ReadAttribute(StringView(temporary), value);
            temporary = "overwritten with other text so a dangling view would read it";
        }
        reader.EndObject();
        reader.FinishLoad();

        const String warnings = Warnings(reader);
        EXPECT_NE(warnings.find("Unknown attribute 'opacitty', did you mean 'opacity'?"), String::npos) << warnings.c_str();
    }

}  // namespace BECore::Tests
