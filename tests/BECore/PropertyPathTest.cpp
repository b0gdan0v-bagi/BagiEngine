// The grammar of a property path (Reflection/PropertyPath.h): what is a path, what is refused and where.

#include <BECore/Random/Rng.h>
#include <BECore/Reflection/PropertyPath.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace PropertyPathLimits;

        /// A segment written as a test reads it: `name`, `[2]` or `["key"]`
        String Describe(const PathSegment& segment) {
            switch (segment.kind) {
                case PathSegmentKind::Field:
                    return segment.text;
                case PathSegmentKind::Index:
                    return Format("[{}]", segment.index);
                case PathSegmentKind::Key:
                    return Format("[\"{}\"]", segment.text);
            }
            return {};
        }

        String Describe(const PropertyPath& path) {
            String out;
            for (const PathSegment& segment : path.segments) {
                if (!out.empty()) {
                    out += ' ';
                }
                out += Describe(segment);
            }
            return out;
        }

        struct ValidCase {
            StringView text;
            StringView segments;
        };

        TEST(PropertyPathTest, ValidPathsGiveTheirSegments) {
            const ValidCase cases[] = {
                {"name", "name"},
                {"_opacity", "_opacity"},
                {"a.b", "a b"},
                {"a[2]", "a [2]"},
                {"a[0]", "a [0]"},
                {"a[007]", "a [7]"},
                {"a.b[2].c", "a b [2] c"},
                {"a[\"text key\"]", "a [\"text key\"]"},
                {"a[\"\"]", "a [\"\"]"},
                {"a[\"x\"][3]", "a [\"x\"] [3]"},
                {"a[1][2]", "a [1] [2]"},
                {"a[\"q\\\"x\\\\y\"]", "a [\"q\"x\\y\"]"},
                {"a[\"dotted.key[1]\"].b", "a [\"dotted.key[1]\"] b"},
                {"a[4294967295]", "a [4294967295]"},
                {"A9_z.B", "A9_z B"},
            };
            for (const ValidCase& testCase : cases) {
                const auto path = ParsePropertyPath(testCase.text);
                ASSERT_TRUE(path.has_value()) << String(testCase.text).c_str() << ": " << (path.has_value() ? String{} : path.error().message).c_str();
                EXPECT_EQ(Describe(*path), String(testCase.segments)) << String(testCase.text).c_str();
            }
        }

        TEST(PropertyPathTest, SegmentsKeepTheirKind) {
            const auto path = ParsePropertyPath("a[2][\"k\"].b");
            ASSERT_TRUE(path.has_value());
            ASSERT_EQ(path->segments.size(), 4U);
            EXPECT_EQ(path->segments[0].kind, PathSegmentKind::Field);
            EXPECT_EQ(path->segments[1].kind, PathSegmentKind::Index);
            EXPECT_EQ(path->segments[1].index, 2U);
            EXPECT_EQ(path->segments[2].kind, PathSegmentKind::Key);
            EXPECT_EQ(path->segments[2].text, "k");
            EXPECT_EQ(path->segments[3].kind, PathSegmentKind::Field);
        }

        struct RefusedCase {
            StringView text;
            PropertyErrorCode code;
            size_t position;
            StringView message;
        };

        TEST(PropertyPathTest, RefusalsNameTheirPosition) {
            const RefusedCase cases[] = {
                {"", PropertyErrorCode::invalid_path, 0, "position 0: path is empty"},
                {".a", PropertyErrorCode::invalid_path, 0, "position 0: expected a name"},
                {"1a", PropertyErrorCode::invalid_path, 0, "position 0: expected a name"},
                {"[1].x", PropertyErrorCode::invalid_path, 0, "position 0: expected a name"},
                {"[\"k\"]", PropertyErrorCode::invalid_path, 0, "position 0: expected a name"},
                {"a.", PropertyErrorCode::invalid_path, 2, "position 2: expected a name"},
                {"a..b", PropertyErrorCode::invalid_path, 2, "position 2: expected a name"},
                {"a.1", PropertyErrorCode::invalid_path, 2, "position 2: expected a name"},
                {"a b", PropertyErrorCode::invalid_path, 1, "position 1: expected '.', '[' or the end of the path"},
                {"a]", PropertyErrorCode::invalid_path, 1, "position 1: expected '.', '[' or the end of the path"},
                {"a[", PropertyErrorCode::invalid_path, 2, "position 2: expected an index or a quoted key"},
                {"a[]", PropertyErrorCode::invalid_path, 2, "position 2: expected an index or a quoted key"},
                {"a[-1]", PropertyErrorCode::invalid_path, 2, "position 2: expected an index or a quoted key"},
                {"a[x]", PropertyErrorCode::invalid_path, 2, "position 2: expected an index or a quoted key"},
                {"a[1", PropertyErrorCode::invalid_path, 3, "position 3: expected ']'"},
                {"a[01x]", PropertyErrorCode::invalid_path, 4, "position 4: expected ']'"},
                {"a[1.5]", PropertyErrorCode::invalid_path, 3, "position 3: expected ']'"},
                {"a[4294967296]", PropertyErrorCode::invalid_path, 2, "position 2: index is larger than 4294967295"},
                {"a[99999999999999999999999]", PropertyErrorCode::invalid_path, 2, "position 2: index is larger than 4294967295"},
                {"a[\"x]", PropertyErrorCode::invalid_path, 5, "position 5: expected '\"' to close the key"},
                {"a[\"x", PropertyErrorCode::invalid_path, 4, "position 4: expected '\"' to close the key"},
                {"a[\"x\"", PropertyErrorCode::invalid_path, 5, "position 5: expected ']'"},
                {"a[\"x\"b]", PropertyErrorCode::invalid_path, 5, "position 5: expected ']'"},
                {"a[\"\\n\"]", PropertyErrorCode::invalid_path, 3, "position 3: invalid escape, only \\\" and \\\\ exist"},
                {"a[\"x\\", PropertyErrorCode::invalid_path, 4, "position 4: invalid escape, only \\\" and \\\\ exist"},
                {"a\tb", PropertyErrorCode::invalid_path, 1, "position 1: control character 0x09"},
                {"a[\"x\ny\"]", PropertyErrorCode::invalid_path, 4, "position 4: control character 0x0a"},
                {"a\x7f", PropertyErrorCode::invalid_path, 1, "position 1: control character 0x7f"},
            };
            for (const RefusedCase& testCase : cases) {
                const auto path = ParsePropertyPath(testCase.text);
                ASSERT_FALSE(path.has_value()) << String(testCase.text).c_str();
                EXPECT_EQ(path.error().code, testCase.code) << String(testCase.text).c_str();
                EXPECT_EQ(path.error().position, testCase.position) << String(testCase.text).c_str();
                EXPECT_EQ(path.error().message, String(testCase.message)) << String(testCase.text).c_str();
            }
        }

        TEST(PropertyPathTest, ANulInsideIsAControlCharacter) {
            const char text[] = {'a', '.', '\0', 'b'};
            const auto path = ParsePropertyPath(StringView(text, sizeof(text)));
            ASSERT_FALSE(path.has_value());
            EXPECT_EQ(path.error().position, 2U);
            EXPECT_EQ(path.error().message, "position 2: control character 0x00");

            const char inKey[] = {'a', '[', '"', '\0', '"', ']'};
            const auto keyed = ParsePropertyPath(StringView(inKey, sizeof(inKey)));
            ASSERT_FALSE(keyed.has_value());
            EXPECT_EQ(keyed.error().position, 3U);
        }

        TEST(PropertyPathTest, BytesAboveAsciiAreFineInAKeyAndNotInAName) {
            const auto key = ParsePropertyPath("a[\"\xd0\xb0\"]");
            ASSERT_TRUE(key.has_value());
            EXPECT_EQ(key->segments[1].text, "\xd0\xb0");
            EXPECT_FALSE(ParsePropertyPath("\xd0\xb0").has_value());
        }

        TEST(PropertyPathTest, EveryLimitIsKeptAndRefusedOneOver) {
            const String longest(kMaxPathBytes, 'a');
            const auto atLimit = ParsePropertyPath(longest);
            ASSERT_TRUE(atLimit.has_value());
            EXPECT_EQ(atLimit->segments[0].text.size(), kMaxPathBytes);

            const auto overLength = ParsePropertyPath(String(kMaxPathBytes + 1, 'a'));
            ASSERT_FALSE(overLength.has_value());
            EXPECT_EQ(overLength.error().code, PropertyErrorCode::too_long);
            EXPECT_EQ(overLength.error().position, kMaxPathBytes);

            String segments;
            for (size_t i = 0; i < kMaxPathSegments; ++i) {
                segments += i == 0 ? "a" : ".a";
            }
            const auto atSegments = ParsePropertyPath(segments);
            ASSERT_TRUE(atSegments.has_value());
            EXPECT_EQ(atSegments->segments.size(), kMaxPathSegments);

            segments += ".a";
            const auto overSegments = ParsePropertyPath(segments);
            ASSERT_FALSE(overSegments.has_value());
            EXPECT_EQ(overSegments.error().code, PropertyErrorCode::too_long);
            EXPECT_EQ(overSegments.error().position, segments.size() - 1);

            String bracketed = "a";
            for (size_t i = 1; i <= kMaxPathSegments; ++i) {
                bracketed += "[1]";
            }
            const auto overBrackets = ParsePropertyPath(bracketed);
            ASSERT_FALSE(overBrackets.has_value());
            EXPECT_EQ(overBrackets.error().code, PropertyErrorCode::too_long);

            const auto keyAtLimit = ParsePropertyPath(Format("a[\"{}\"]", String(kMaxKeyBytes, 'k')));
            ASSERT_TRUE(keyAtLimit.has_value());
            const auto keyOver = ParsePropertyPath(Format("a[\"{}\"]", String(kMaxKeyBytes + 1, 'k')));
            ASSERT_FALSE(keyOver.has_value());
            EXPECT_EQ(keyOver.error().code, PropertyErrorCode::too_long);

            // Escapes count after they are resolved
            String escaped;
            for (size_t i = 0; i < kMaxKeyBytes; ++i) {
                escaped += "\\\\";
            }
            const auto escapedAtLimit = ParsePropertyPath(Format("a[\"{}\"]", escaped));
            EXPECT_EQ(escapedAtLimit.has_value(), 4 + escaped.size() <= kMaxPathBytes);
        }

        TEST(PropertyPathTest, FormatGivesTheFirstSegmentsBack) {
            const auto path = ParsePropertyPath("a.b[2][\"q\\\"x\"].c");
            ASSERT_TRUE(path.has_value());
            EXPECT_EQ(FormatPropertyPath(path->segments, path->segments.size()), "a.b[2][\"q\\\"x\"].c");
            EXPECT_EQ(FormatPropertyPath(path->segments, 3), "a.b[2]");
            EXPECT_EQ(FormatPropertyPath(path->segments, 1), "a");
            EXPECT_EQ(FormatPropertyPath(path->segments, 0), "");
            EXPECT_EQ(FormatPropertyPath(path->segments, 99), "a.b[2][\"q\\\"x\"].c");
        }

        TEST(PropertyPathTest, ParsedPathFormatsToAPathThatParsesTheSame) {
            const auto path = ParsePropertyPath("x[\"a\\\\b\"][7].y");
            ASSERT_TRUE(path.has_value());
            const String text = FormatPropertyPath(path->segments, path->segments.size());
            const auto again = ParsePropertyPath(text);
            ASSERT_TRUE(again.has_value());
            EXPECT_EQ(Describe(*again), Describe(*path));
        }

        char RandomByte(Rng& rng) {
            // Leans on the characters of the grammar so that deep branches are reached
            const char alphabet[] = "ab_9.[]\"\\-0 ";
            if (rng.UniformInt<int>(0, 5) == 0) {
                return static_cast<char>(rng.UniformInt<int>(0, 255));
            }
            return alphabet[rng.UniformInt<size_t>(0, sizeof(alphabet) - 2)];
        }

        TEST(PropertyPathTest, RandomBytesNeverCrashAndWhatParsesRoundTrips) {
            const StringView seeds[] = {"a.b[2].c", "a[\"k\\\"x\"].b", "name", "a[1][2][3]", "x.y.z"};
            Rng rng(0x5eed);
            size_t parsed = 0;
            for (size_t round = 0; round < 5000; ++round) {
                String text;
                if (round % 2 == 0) {
                    const size_t length = rng.UniformInt<size_t>(0, 40);
                    for (size_t i = 0; i < length; ++i) {
                        text.push_back(RandomByte(rng));
                    }
                } else {
                    // A valid path with up to three bytes changed, inserted or dropped
                    text = String(seeds[rng.UniformInt<size_t>(0, std::size(seeds) - 1)]);
                    for (int edits = rng.UniformInt<int>(0, 3); edits > 0 && !text.empty(); --edits) {
                        const size_t at = rng.UniformInt<size_t>(0, text.size() - 1);
                        switch (rng.UniformInt<int>(0, 2)) {
                            case 0:
                                text[at] = RandomByte(rng);
                                break;
                            case 1:
                                text.insert(text.begin() + static_cast<std::ptrdiff_t>(at), RandomByte(rng));
                                break;
                            default:
                                text.erase(text.begin() + static_cast<std::ptrdiff_t>(at));
                                break;
                        }
                    }
                }
                const auto path = ParsePropertyPath(text);
                if (!path.has_value()) {
                    EXPECT_LE(path.error().position, text.size());
                    EXPECT_FALSE(path.error().message.empty());
                    continue;
                }
                ++parsed;
                const auto again = ParsePropertyPath(FormatPropertyPath(path->segments, path->segments.size()));
                ASSERT_TRUE(again.has_value()) << String(text).c_str();
                EXPECT_EQ(Describe(*again), Describe(*path));
            }
            EXPECT_GT(parsed, 500U);
        }

    }  // namespace

}  // namespace BECore::Tests
