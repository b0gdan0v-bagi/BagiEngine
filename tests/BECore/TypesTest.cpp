#include <Common/Printers.h>
#include <limits>
#include <string>
#include <string_view>

namespace BECore::Tests {

    template <typename M, typename K>
    concept LooksUpBy = requires(M & map, const M& constMap, const K& key) {
        map.find(key);
        constMap.find(key);
        constMap.contains(key);
        constMap.count(key);
        constMap.at(key);
    };

    // The aliases that are a plain `using`: the same type as the eastl:: / std:: name.
    static_assert(std::is_same_v<String, eastl::string>);
    static_assert(std::is_same_v<StringView, eastl::string_view>);
    static_assert(std::is_same_v<FixedString<32>, eastl::fixed_string<char, 32, true>>);
    static_assert(std::is_same_v<FixedString<32, false>, eastl::fixed_string<char, 32, false>>);
    static_assert(std::is_same_v<Vector<int>, eastl::vector<int>>);
    static_assert(std::is_same_v<SmallVector<int, 4>, eastl::fixed_vector<int, 4, true>>);
    static_assert(std::is_same_v<FixedVector<int, 4>, eastl::fixed_vector<int, 4, false>>);
    static_assert(std::is_same_v<Array<int, 3>, eastl::array<int, 3>>);
    static_assert(std::is_same_v<Span<int>, eastl::span<int>>);
    static_assert(std::is_same_v<Span<int, 3>, eastl::span<int, 3>>);
    static_assert(std::is_same_v<Optional<int>, std::optional<int>>);
    static_assert(std::is_same_v<UniquePtr<int>, eastl::unique_ptr<int>>);
    static_assert(std::is_same_v<UniquePtr<int[]>, eastl::unique_ptr<int[]>>);
    static_assert(std::is_same_v<HashMap<int, int>, eastl::hash_map<int, int, eastl::hash<int>, eastl::equal_to<int>>>);
    static_assert(std::is_same_v<Map<int, int>, eastl::map<int, int, eastl::less<int>>>);
    static_assert(std::is_same_v<FlatMap<int, int>, eastl::vector_map<int, int, eastl::less<int>>>);
    static_assert(std::is_same_v<FlatSet<int>, eastl::vector_set<int, eastl::less<int>>>);

    // A String key gets the transparent hash, equality and order; any other key keeps the EASTL ones.
    static_assert(std::is_same_v<HashMap<String, int>, eastl::hash_map<String, int, StringHash, StringEqual>>);
    static_assert(std::is_same_v<Map<String, int>, eastl::map<String, int, StringLess>>);
    static_assert(std::is_same_v<FixedHashMap<String, int, 8>, eastl::fixed_hash_map<String, int, 8, 9, true, StringHash, StringEqual>>);
    static_assert(std::is_same_v<FixedHashMap<int, int, 8>, eastl::fixed_hash_map<int, int, 8, 9, true, eastl::hash<int>, eastl::equal_to<int>>>);
    static_assert(std::is_same_v<FlatMap<String, int>, eastl::vector_map<String, int, StringLess>>);

    // A map of String keys finds by a view and a literal without making a String; the map of eastl::string keys does not.
    static_assert(LooksUpBy<HashMap<String, int>, StringView>);
    static_assert(LooksUpBy<HashMap<String, int>, String>);
    static_assert(LooksUpBy<HashMap<String, int>, char[4]>);
    static_assert(LooksUpBy<Map<String, int>, StringView>);
    static_assert(LooksUpBy<Map<String, int>, char[4]>);
    static_assert(LooksUpBy<FixedHashMap<String, int, 8>, StringView>);
    static_assert(!LooksUpBy<eastl::hash_map<eastl::string, int>, StringView>);
    static_assert(!LooksUpBy<eastl::map<eastl::string, int>, StringView>);

    TEST(TypesTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts at the top of this file are the check";
    }

    TEST(TypesTest, StringHashEqualsTheHashOfEastlString) {
        const char embedded[] = {'a', '\xE4', '\xFF', 'z', '\0'};
        const eastl::string samples[] = {
            "", "a", "Mahjong", "a string that is longer than the small-string buffer of the type, 0123456789", eastl::string{embedded}, eastl::string{"\x80\x81"},
        };
        for (const eastl::string& sample : samples) {
            const size_t expected = eastl::hash<eastl::string>{}(sample);
            EXPECT_EQ(StringHash{}(sample), expected) << sample.c_str();
            EXPECT_EQ(StringHash{}(StringView{sample}), expected) << sample.c_str();
            EXPECT_EQ(StringHash{}(sample.c_str()), expected) << sample.c_str();
        }
    }

    TEST(TypesTest, StringEqualAndLessAgreeAcrossTheForms) {
        const String text = "alpha";
        EXPECT_TRUE(StringEqual{}(text, StringView{"alpha"}));
        EXPECT_TRUE(StringEqual{}("alpha", text));
        EXPECT_FALSE(StringEqual{}(text, "alphabet"));
        EXPECT_TRUE(StringLess{}(text, "alphabet"));
        EXPECT_TRUE(StringLess{}("alpha", String{"beta"}));
        EXPECT_FALSE(StringLess{}(text, StringView{"alpha"}));
        // Bytes compare as unsigned, as eastl::string's operator< does
        EXPECT_EQ(StringLess{}(String{"\x80"}, String{"a"}), String{"\x80"} < String{"a"});
    }

    TEST(TypesTest, HashMapOfStringsIteratesInTheOrderOfTheEastlMap) {
        HashMap<String, int> map;
        eastl::hash_map<eastl::string, int> reference;
        for (int i = 0; i < 200; ++i) {
            const String key = Format("key_{}_{}", i, i * 7919);
            map[key] = i;
            reference[eastl::string{key.c_str()}] = i;
        }
        ASSERT_EQ(map.size(), reference.size());
        auto itMap = map.begin();
        for (auto itReference = reference.begin(); itReference != reference.end(); ++itReference, ++itMap) {
            EXPECT_EQ(itMap->first, itReference->first);
            EXPECT_EQ(itMap->second, itReference->second);
        }
    }

    TEST(TypesTest, HashMapOfStringsLooksUpByViewAndLiteral) {
        HashMap<String, int> map;
        map["one"] = 1;
        map[String{"two"}] = 2;
        map.emplace("three", 3);

        const auto& constMap = map;
        ASSERT_NE(constMap.find("one"), constMap.end());
        EXPECT_EQ(constMap.find("one")->second, 1);
        EXPECT_EQ(constMap.find(StringView{"two"})->second, 2);
        EXPECT_EQ(constMap.find(String{"three"})->second, 3);
        EXPECT_EQ(constMap.find(StringView{"four"}), constMap.end());
        EXPECT_EQ(constMap.find(StringView{"on"}), constMap.end());
        EXPECT_TRUE(constMap.contains(StringView{"two"}));
        EXPECT_FALSE(constMap.contains("TWO"));
        EXPECT_EQ(constMap.count("three"), 1u);
        EXPECT_EQ(constMap.count("none"), 0u);
        EXPECT_EQ(constMap.at(StringView{"one"}), 1);
        EXPECT_EQ(map.erase(StringView{"one"}), 1u);
        EXPECT_FALSE(map.contains("one"));
    }

    TEST(TypesTest, MapOfStringsLooksUpByViewAndKeepsTheOrder) {
        Map<String, int> map;
        eastl::map<eastl::string, int> reference;
        for (const char* key : {"pear", "apple", "fig", "\x80z", "Apple", "", "applesauce"}) {
            map[key] = static_cast<int>(map.size());
            reference[eastl::string{key}] = static_cast<int>(reference.size());
        }
        auto itMap = map.begin();
        for (auto itReference = reference.begin(); itReference != reference.end(); ++itReference, ++itMap) {
            EXPECT_EQ(itMap->first, itReference->first);
            EXPECT_EQ(itMap->second, itReference->second);
        }
        const auto& constMap = map;
        ASSERT_NE(constMap.find(StringView{"fig"}), constMap.end());
        EXPECT_EQ(constMap.find("fig")->second, reference["fig"]);
        EXPECT_TRUE(constMap.contains(StringView{"apple"}));
        EXPECT_EQ(constMap.count("pea"), 0u);
        EXPECT_EQ(constMap.at(StringView{"pear"}), reference["pear"]);
    }

    TEST(TypesTest, FixedHashMapOfStringsLooksUpByView) {
        FixedHashMap<String, int, 4> map;
        map["a"] = 1;
        map["b"] = 2;
        EXPECT_EQ(map.find(StringView{"b"})->second, 2);
        EXPECT_FALSE(map.contains("c"));
    }

    TEST(TypesTest, FlatMapKeepsItsKeysSorted) {
        FlatMap<String, int> map;
        map["pear"] = 3;
        map["apple"] = 1;
        map["fig"] = 2;
        ASSERT_EQ(map.size(), 3u);
        EXPECT_EQ(map.begin()->first, "apple");
        EXPECT_EQ(map.find(String{"fig"})->second, 2);
        FlatSet<int> set;
        set.insert(5);
        set.insert(1);
        EXPECT_EQ(*set.begin(), 1);
    }

    TEST(TypesTest, SmallVectorGrowsPastItsInlineStorageAndFixedVectorDoesNot) {
        SmallVector<int, 2> small;
        for (int i = 0; i < 5; ++i) {
            small.push_back(i);
        }
        EXPECT_EQ(small.size(), 5u);
        EXPECT_TRUE(small.has_overflowed());

        FixedVector<int, 2> fixed;
        fixed.push_back(1);
        fixed.push_back(2);
        EXPECT_TRUE(fixed.full());
        EXPECT_FALSE(fixed.has_overflowed());
        EXPECT_EQ(fixed.capacity(), 2u);
    }

    TEST(TypesTest, ToStringOfIntegers) {
        EXPECT_EQ(ToString(0), "0");
        EXPECT_EQ(ToString(-42), "-42");
        EXPECT_EQ(ToString(7u), "7");
        EXPECT_EQ(ToString(std::numeric_limits<int64_t>::min()), "-9223372036854775808");
        EXPECT_EQ(ToString(std::numeric_limits<uint64_t>::max()), "18446744073709551615");
        EXPECT_EQ(ToString(static_cast<uint8_t>(65)), "65");
        EXPECT_EQ(ToString(static_cast<int8_t>(-3)), "-3");
        EXPECT_EQ(ToString(size_t{12}), "12");
    }

    TEST(TypesTest, ToStringOfFloatingPointWritesWhatStdToStringWrites) {
        for (const double value : {0.0, 1.5, -2.25, 3.14159265358979, 1e-7, 123456789.125}) {
            const std::string expected = std::to_string(value);
            EXPECT_EQ(ToString(value), String{expected.c_str()}) << expected;
        }
        EXPECT_EQ(ToString(1.5f), "1.500000");
    }

    TEST(TypesTest, ToStringDoesNotTakeCharactersOrBool) {
        const auto callable = []<typename T>(T) {
            return requires(T value) {
                ToString(value);
            };
        };
        EXPECT_TRUE(callable(1));
        EXPECT_FALSE(callable(true));
        EXPECT_FALSE(callable('c'));
    }

    TEST(TypesTest, FormatReturnsAStringAndAppendFormatAddsToOne) {
        const String text = Format("{}-{:03d}-{}", "x", 7, StringView{"y"});
        EXPECT_EQ(text, "x-007-y");

        const String longText = Format("{}", String(400, 'q'));
        EXPECT_EQ(longText.size(), 400u);

        String out = "head:";
        AppendFormat(out, "{} {}", 1, String{"two"});
        AppendFormat(out, "!");
        EXPECT_EQ(out, "head:1 two!");
    }

    TEST(TypesTest, FormatTakesAStringOfAnotherAllocator) {
        struct OtherAllocator : eastl::allocator {
            using eastl::allocator::allocator;
        };
        static_assert(!std::is_same_v<eastl::basic_string<char, OtherAllocator>, String>);
        const eastl::basic_string<char, OtherAllocator> other = "other";
        EXPECT_EQ(Format("{}", other), "other");
    }

    TEST(TypesTest, ConversionsToAndFromStdRoundTrip) {
        const String text = "round trip \xE4";
        const std::string_view asStd = ToStd(text);
        ASSERT_EQ(asStd.size(), text.size());
        EXPECT_EQ(asStd.data(), text.data());
        EXPECT_EQ(FromStd(asStd), StringView{text});
        EXPECT_EQ(FromStd(asStd).data(), text.data());

        const std::string copy = ToStdString(text);
        EXPECT_EQ(copy, std::string(text.data(), text.size()));
        EXPECT_NE(copy.data(), text.data());
        EXPECT_EQ(FromStdString(copy), text);
        EXPECT_EQ(FromStdString(std::string_view{}), String{});

        const String withNul{"a\0b", 3};
        EXPECT_EQ(FromStdString(ToStdString(withNul)).size(), 3u);

        static_assert(ToStd(StringView{"abc"}) == std::string_view{"abc"});
        static_assert(FromStd(std::string_view{"abc"}) == StringView{"abc"});
    }

}  // namespace BECore::Tests
