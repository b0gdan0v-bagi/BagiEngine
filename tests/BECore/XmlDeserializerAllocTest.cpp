// XmlDeserializer: the Warn mode costs nothing per node over the Off mode (its scopes' lists live in an arena the deserializer owns), and
// an array costs no list of its elements.

#include "XmlDeserializerAllocTest.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Generated/XmlDeserializerAllocTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Past ReaderLimits::kMaxRecordedErrors, so the findings stop growing and only the walk is left to measure
        constexpr size_t kManyGroups = 1000;
        constexpr size_t kFewGroups = 100;
        constexpr size_t kItemsPerGroup = 3;

        /// What a second load by the same deserializer may allocate beyond what the findings and the objects need: a few blocks, never one per node
        constexpr uint64_t kLoadSlack = 8;

        String MakeDocument(size_t groups, bool withUnknownKeys) {
            String text = "<root>\n<groups>\n";
            for (size_t i = 0; i < groups; ++i) {
                AppendFormat(text, "<group id=\"{}\" description=\"short\"{}>\n", i, withUnknownKeys ? " extraAttribute=\"1\"" : "");
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

    TEST(XmlDeserializerAllocTest, ACleanLoadAllocatesNothingPerNodeInEitherMode) {
        const String document = MakeDocument(kManyGroups, false);
        const uint64_t warn = SecondLoadAllocations(document, DiagnosticsMode::Warn).count;
        const uint64_t off = SecondLoadAllocations(document, DiagnosticsMode::Off).count;
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(warn, kLoadSlack);
            EXPECT_LE(off, kLoadSlack);
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

    TEST(XmlDeserializerAllocTest, ACommentBetweenElementsIsNoElementAndNestedArraysKeepTheirPlace) {
        XmlDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(reader.LoadFromString(
            R"(<root><list><!-- first --><row id="1"><cells><c v="10"/><!-- x --><c v="11"/></cells></row><!-- --><row id="2"><cells><c v="20"/></cells></row><!-- end --></list></root>)"));

        size_t rows = 0;
        ASSERT_TRUE(reader.BeginArray("list", "row", rows));
        ASSERT_EQ(rows, 2u);
        Vector<uint32_t> seen;
        while (reader.BeginArrayElement()) {
            uint32_t id = 0;
            reader.ReadAttribute("id", id);
            seen.push_back(id);
            size_t cells = 0;
            ASSERT_TRUE(reader.BeginArray("cells", "c", cells));
            for (size_t i = 0; i < cells; ++i) {
                ASSERT_TRUE(reader.BeginArrayElement());
                uint32_t value = 0;
                reader.ReadAttribute("v", value);
                seen.push_back(value);
                reader.EndArrayElement();
            }
            EXPECT_FALSE(reader.BeginArrayElement()) << "the cursor is past the last cell";
            reader.EndArray();
            reader.EndArrayElement();
        }
        reader.EndArray();
        reader.FinishLoad();

        const Vector<uint32_t> expected = {1, 10, 11, 2, 20};
        EXPECT_TRUE(seen == expected);
        EXPECT_FALSE(reader.HasErrors());
        EXPECT_TRUE(reader.GetWarnings().empty());
    }

    TEST(XmlDeserializerAllocTest, AFactoryArrayOfLongTypeNamesAllocatesOneObjectPerElement) {
        constexpr size_t kThings = 500;
        // A fresh reader's own first blocks: its stacks, and in the Warn mode its arena and scopes
        constexpr uint64_t kFirstUseSlack = 16;
        String document = "<root><things>";
        for (size_t i = 0; i < kThings; ++i) {
            AppendFormat(document, "<thing type=\"AThingWithAnExtraLongTypeNameForTheFactory\" value=\"{}\"/>", i);
        }
        document += "</things></root>";

        for (const DiagnosticsMode mode : {DiagnosticsMode::Off, DiagnosticsMode::Warn}) {
            XmlDeserializer reader;
            reader.SetDiagnostics(mode);
            ASSERT_TRUE(reader.LoadFromString(document));
            auto list = New<LongNamedThingList>();
            const AllocCount allocs = CountAllocations([&] {
                list->Deserialize(reader);
                reader.FinishLoad();
            });
            ASSERT_EQ(list->things.size(), kThings);
            EXPECT_EQ(list->things.back()->value, static_cast<int32_t>(kThings - 1));
            EXPECT_FALSE(reader.HasErrors());
            EXPECT_TRUE(reader.GetWarnings().empty());
            if constexpr (AllocTracker::kEnabled) {
                // The objects themselves and the list's own storage and stacks: no copy of the type name, no list of elements
                EXPECT_LE(allocs.count, kThings + kFirstUseSlack) << "mode " << static_cast<int>(mode);
            }
        }
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST(XmlDeserializerAllocTest, TheSerializerCopiesNeitherNamesNorValues) {
        constexpr size_t kFields = 400;
        constexpr StringView kLongValue = "a value that is well over the twenty-three bytes a small string keeps";
        Vector<String> names;
        for (size_t i = 0; i < kFields; ++i) {
            names.push_back(Format("a_field_whose_name_is_over_the_small_string_{}", i));
        }

        XmlSerializer writer;
        const AllocCount allocs = CountAllocations([&] {
            for (const String& name : names) {
                writer.WriteAttribute(name, kLongValue);
                writer.Write(name, kLongValue);
            }
        });
        if constexpr (AllocTracker::kEnabled) {
            // The DOM takes pages for what it stores, never a block per name or per value
            EXPECT_LT(allocs.count, kFields / 4);
        }

        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString(writer.SaveToString()));
        String first;
        String last;
        EXPECT_TRUE(reader.ReadAttribute(names.front(), first));
        EXPECT_TRUE(reader.Read(names.back(), last));
        EXPECT_EQ(first, String(kLongValue));
        EXPECT_EQ(last, String(kLongValue));
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

}  // namespace BECore::Tests
