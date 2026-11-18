#include <BECore/Config/XmlLayoutMerge.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A hand-laid file: a blank line between the rows
        constexpr StringView kHandLaid = R"(<root>
  <sprites>
    <sprite name="A" x="1" />

    <sprite name="B" x="2" />

    <sprite name="C" x="3" />
  </sprites>
</root>
)";

        constexpr StringView kCanonical = R"(<root>
  <sprites>
    <sprite name="A" x="1" />
    <sprite name="B" x="2" />
    <sprite name="C" x="3" />
  </sprites>
</root>
)";

        String Replaced(StringView text, StringView from, StringView to) {
            String result(text.data(), text.size());
            const size_t at = result.find(from.data(), 0, from.size());
            EXPECT_NE(at, String::npos) << "no '" << String(from.data(), from.size()).c_str() << "' in the text";
            if (at != String::npos) {
                result.replace(at, from.size(), String(to.data(), to.size()));
            }
            return result;
        }

        /// The lines of @p text that are not blank, trimmed, joined by a line break
        String NonBlankTrimmed(StringView text) {
            String result;
            size_t begin = 0;
            while (begin <= text.size()) {
                size_t end = text.find('\n', begin);
                if (end == StringView::npos) {
                    end = text.size();
                }
                StringView line = text.substr(begin, end - begin);
                const size_t first = line.find_first_not_of(" \t");
                if (first != StringView::npos) {
                    line = line.substr(first, line.find_last_not_of(" \t") - first + 1);
                    result.append(line.data(), line.size());
                    result.push_back('\n');
                }
                begin = end + 1;
            }
            return result;
        }

        /// Merges with the original's own canonical form as the baseline; the case tests spell the canonical forms out
        Optional<String> Merge(StringView original, StringView baseline, StringView updated) {
            return MergeXmlLayout(original, baseline, updated);
        }

        String Joined(std::initializer_list<StringView> lines) {  //-NoStd: a braced list of literals at the call
            String result;
            for (const StringView line : lines) {
                result.append(line.data(), line.size());
                result.push_back('\n');
            }
            return result;
        }

    }  // namespace

    TEST(XmlLayoutMergeTest, UnchangedReturnsTheOriginalByteForByte) {
        const Optional<String> merged = Merge(kHandLaid, kCanonical, kCanonical);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, String(kHandLaid.data(), kHandLaid.size()));
    }

    TEST(XmlLayoutMergeTest, UnchangedKeepsAFourSpaceIndentAndTrailingSpaces) {
        const String original = Joined({"<root>", "    <a x=\"1\" />   ", "", "        <b />", "</root>"});
        const String baseline = Joined({"<root>", "  <a x=\"1\" />", "  <b />", "</root>"});
        const Optional<String> merged = Merge(original, baseline, baseline);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, original);
    }

    TEST(XmlLayoutMergeTest, AChangedAttributeChangesOnlyItsLine) {
        const String updated = Replaced(kCanonical, "x=\"2\"", "x=\"20\"");
        const Optional<String> merged = Merge(kHandLaid, kCanonical, updated);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, Replaced(kHandLaid, "x=\"2\"", "x=\"20\""));
    }

    TEST(XmlLayoutMergeTest, AChangedLineKeepsTheIndentOfTheFileWhileItStaysAtItsDepth) {
        const String original = Joined({"<root>", "    <a x=\"1\" />", "    <b x=\"1\" />", "</root>"});
        const String baseline = Joined({"<root>", "  <a x=\"1\" />", "  <b x=\"1\" />", "</root>"});

        const Optional<String> changed = Merge(original, baseline, Joined({"<root>", "  <a x=\"2\" />", "  <b x=\"1\" />", "</root>"}));
        ASSERT_TRUE(changed.has_value());
        EXPECT_EQ(*changed, Joined({"<root>", "    <a x=\"2\" />", "    <b x=\"1\" />", "</root>"}));
    }

    TEST(XmlLayoutMergeTest, AReplacementOfADifferentNumberOfLinesTakesTheCanonicalLines) {
        const String original = Joined({"<root>", "    <a />", "</root>"});
        const String baseline = Joined({"<root>", "  <a />", "</root>"});
        const String updated = Joined({"<root>", "  <a>", "    <b />", "  </a>", "</root>"});

        const Optional<String> merged = Merge(original, baseline, updated);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, updated);
    }

    TEST(XmlLayoutMergeTest, ALineInsertedAtTheStartGoesBeforeTheFirstLine) {
        const String updated = Joined(
            {"<!-- header -->", "<root>", "  <sprites>", "    <sprite name=\"A\" x=\"1\" />", "    <sprite name=\"B\" x=\"2\" />", "    <sprite name=\"C\" x=\"3\" />", "  </sprites>", "</root>"});
        const Optional<String> merged = Merge(kHandLaid, kCanonical, updated);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, String("<!-- header -->\n") + String(kHandLaid.data(), kHandLaid.size()));
    }

    TEST(XmlLayoutMergeTest, ARowInsertedInTheMiddleGoesDirectlyBeforeTheRowThatFollowsIt) {
        const String updated = Replaced(kCanonical, "    <sprite name=\"B\" x=\"2\" />\n", "    <sprite name=\"AB\" x=\"9\" />\n    <sprite name=\"B\" x=\"2\" />\n");
        const Optional<String> merged = Merge(kHandLaid, kCanonical, updated);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, Replaced(kHandLaid, "\n    <sprite name=\"B\" x=\"2\" />\n", "\n    <sprite name=\"AB\" x=\"9\" />\n    <sprite name=\"B\" x=\"2\" />\n"));
    }

    TEST(XmlLayoutMergeTest, ARowInsertedAtTheEndOfABlockGoesBeforeTheClosingTag) {
        const String updated = Replaced(kCanonical, "  </sprites>\n", "    <sprite name=\"D\" x=\"4\" />\n  </sprites>\n");
        const Optional<String> merged = Merge(kHandLaid, kCanonical, updated);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, Replaced(kHandLaid, "  </sprites>\n", "    <sprite name=\"D\" x=\"4\" />\n  </sprites>\n"));
    }

    TEST(XmlLayoutMergeTest, ALineInsertedAtTheEndOfTheFileGoesAfterTheLastLine) {
        const String updated = String(kCanonical.data(), kCanonical.size()) + "<!-- tail -->\n";
        const Optional<String> merged = Merge(kHandLaid, kCanonical, updated);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, String(kHandLaid.data(), kHandLaid.size()) + "<!-- tail -->\n");
    }

    TEST(XmlLayoutMergeTest, ADeletedRowTakesItsLineAndKeepsTheBlankLineNextToIt) {
        const String updated = Replaced(kCanonical, "    <sprite name=\"B\" x=\"2\" />\n", "");
        const Optional<String> merged = Merge(kHandLaid, kCanonical, updated);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, Replaced(kHandLaid, "    <sprite name=\"B\" x=\"2\" />\n", ""));
        EXPECT_EQ(NonBlankTrimmed(*merged), NonBlankTrimmed(updated));
    }

    TEST(XmlLayoutMergeTest, SeveralHunksAreEachPlacedByTheirOwnLine) {
        String updated = Replaced(kCanonical, "x=\"1\"", "x=\"10\"");
        updated = Replaced(updated, "    <sprite name=\"B\" x=\"2\" />\n", "");
        updated = Replaced(updated, "  </sprites>\n", "    <sprite name=\"D\" x=\"4\" />\n  </sprites>\n");
        const Optional<String> merged = Merge(kHandLaid, kCanonical, updated);
        ASSERT_TRUE(merged.has_value());
        const String expected =
            Joined({"<root>", "  <sprites>", "    <sprite name=\"A\" x=\"10\" />", "", "", "    <sprite name=\"C\" x=\"3\" />", "    <sprite name=\"D\" x=\"4\" />", "  </sprites>", "</root>"});
        EXPECT_EQ(NonBlankTrimmed(*merged), NonBlankTrimmed(updated));
        EXPECT_NE(merged->find("\n\n"), String::npos);
        EXPECT_NE(NonBlankTrimmed(expected).size(), 0u);
    }

    TEST(XmlLayoutMergeTest, ALineChangedInsideAMultiLineCommentIsReplacedAndItsBlankLineStays) {
        const String original = Joined({"<root>", "  <!-- one", "", "     two -->", "", "  <a />", "</root>"});
        const String baseline = Joined({"<root>", "  <!-- one", "", "     two -->", "  <a />", "</root>"});
        const String updated = Joined({"<root>", "  <!-- one", "", "     three -->", "  <a />", "</root>"});
        const Optional<String> merged = Merge(original, baseline, updated);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, Joined({"<root>", "  <!-- one", "", "     three -->", "", "  <a />", "</root>"}));
    }

    TEST(XmlLayoutMergeTest, ABlankLineInsideACommentThatTheOriginalLacksIsNoMatch) {
        const String original = Joined({"<root>", "  <!-- one", "     two -->", "</root>"});
        const String baseline = Joined({"<root>", "  <!-- one", "", "     two -->", "</root>"});
        const String updated = Joined({"<root>", "  <!-- one", "", "     two -->", "  <a />", "</root>"});
        EXPECT_FALSE(Merge(original, baseline, updated).has_value());
    }

    TEST(XmlLayoutMergeTest, ATextWhoseLinesDoNotMatchTheBaselineIsRefused) {
        const String updated = Replaced(kCanonical, "x=\"1\"", "x=\"10\"");
        EXPECT_FALSE(Merge("<root>\n  <a/>\n</root>\n", "<root>\n  <a />\n</root>\n", "<root>\n  <a x=\"1\" />\n</root>\n").has_value());
        EXPECT_FALSE(Merge("<root>\n  <a x='1' />\n</root>\n", "<root>\n  <a x=\"1\" />\n</root>\n", "<root>\n  <a x=\"2\" />\n</root>\n").has_value());
        EXPECT_FALSE(Merge("<root>\n  <a\n     x=\"1\" />\n</root>\n", "<root>\n  <a x=\"1\" />\n</root>\n", "<root>\n  <a x=\"2\" />\n</root>\n").has_value());
        EXPECT_FALSE(Merge("<root>\n  <a x=\"1\" />\n</root>\n", kCanonical, updated).has_value());
    }

    TEST(XmlLayoutMergeTest, AnUnchangedTextThatDoesNotMatchTheBaselineIsRefusedToo) {
        EXPECT_FALSE(Merge("<root>\n  <a/>\n</root>\n", "<root>\n  <a />\n</root>\n", "<root>\n  <a />\n</root>\n").has_value());
    }

    TEST(XmlLayoutMergeTest, ALineBreakWithACarriageReturnIsRefused) {
        const String original = "<root>\r\n  <a />\r\n</root>\r\n";
        const String baseline = "<root>\n  <a />\n</root>\n";
        EXPECT_FALSE(Merge(original, baseline, "<root>\n  <a x=\"1\" />\n</root>\n").has_value());
        EXPECT_FALSE(Merge(baseline, baseline, "<root>\r\n</root>\n").has_value());
    }

    TEST(XmlLayoutMergeTest, AnEmptyOriginalIsRefused) {
        EXPECT_FALSE(Merge("", "", "<root />\n").has_value());
        EXPECT_FALSE(Merge("\n\n", "", "<root />\n").has_value());
    }

    TEST(XmlLayoutMergeTest, TheEndOfTheTextFollowsTheOriginalUnlessTheLastLineChanged) {
        const String original = "<root>\n  <a />\n</root>";
        const String baseline = "<root>\n  <a />\n</root>\n";
        const Optional<String> unchanged = Merge(original, baseline, baseline);
        ASSERT_TRUE(unchanged.has_value());
        EXPECT_EQ(*unchanged, original);

        const Optional<String> inner = Merge(original, baseline, "<root>\n  <a x=\"1\" />\n</root>\n");
        ASSERT_TRUE(inner.has_value());
        EXPECT_EQ(*inner, "<root>\n  <a x=\"1\" />\n</root>");

        const Optional<String> last = Merge(original, baseline, "<root>\n  <a />\n</root2>\n");
        ASSERT_TRUE(last.has_value());
        EXPECT_EQ(*last, "<root>\n  <a />\n</root2>\n");
    }

    TEST(XmlLayoutMergeTest, ABigFileWithEditsAtBothEndsIsMerged) {
        String baseline = "<root>\n";
        for (int i = 0; i < 3000; ++i) {
            baseline += Format("  <a n=\"{}\" />\n", i);
        }
        baseline += "</root>\n";
        const String updated = Replaced(Replaced(baseline, "n=\"0\"", "n=\"x\""), "n=\"2999\"", "n=\"y\"");

        const Optional<String> merged = Merge(baseline, baseline, updated);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(*merged, updated);
    }

    TEST(XmlLayoutMergeTest, ATextPastTheLineLimitIsRefused) {
        String text = "<root>\n";
        for (int i = 0; i < 20001; ++i) {
            text += "  <a />\n";
        }
        text += "</root>\n";
        EXPECT_FALSE(Merge(text, text, text + "<!-- tail -->\n").has_value());
    }

    TEST(XmlLayoutMergeTest, ASeededRunOfEditsKeepsTheLinesOfTheUpdatedText) {
        const String original = Joined({"<root>", "    <a n=\"0\" />", "", "    <a n=\"1\" />", "    <a n=\"2\" />", "", "", "    <a n=\"3\" />", "    <a n=\"4\" />", "</root>"});
        const String baseline = Joined({"<root>", "  <a n=\"0\" />", "  <a n=\"1\" />", "  <a n=\"2\" />", "  <a n=\"3\" />", "  <a n=\"4\" />", "</root>"});

        uint32_t seed = 12345;
        const auto next = [&seed](uint32_t bound) {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 16) % bound;
        };

        for (int run = 0; run < 200; ++run) {
            Vector<String> lines;
            size_t begin = 0;
            while (begin < baseline.size()) {
                const size_t end = baseline.find('\n', begin);
                lines.push_back(baseline.substr(begin, end - begin));
                begin = end + 1;
            }
            const uint32_t edits = 1 + next(4);
            for (uint32_t edit = 0; edit < edits; ++edit) {
                const size_t at = 1 + next(static_cast<uint32_t>(lines.size() - 1));
                switch (next(3)) {
                    case 0:
                        lines.insert(lines.begin() + static_cast<ptrdiff_t>(at), Format("  <b n=\"{}\" />", next(100)));
                        break;
                    case 1:
                        if (lines.size() > 3 && at < lines.size() - 1) {
                            lines.erase(lines.begin() + static_cast<ptrdiff_t>(at));
                        }
                        break;
                    default:
                        if (at < lines.size() - 1) {
                            lines[at] = Format("  <c n=\"{}\" />", next(100));
                        }
                        break;
                }
            }
            String updated;
            for (const String& line : lines) {
                updated += line;
                updated += '\n';
            }

            const Optional<String> merged = Merge(original, baseline, updated);
            ASSERT_TRUE(merged.has_value()) << "run " << run;
            EXPECT_EQ(NonBlankTrimmed(*merged), NonBlankTrimmed(updated)) << "run " << run;
        }
    }

}  // namespace BECore::Tests
