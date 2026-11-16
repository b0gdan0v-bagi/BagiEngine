#include <BECore/Control/ControlService.h>
#include <Common/Printers.h>
#include <Games/ReplayFile.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kHeaderLine = R"({"replay":1,"game":"Dice","engine":"0.1.5"})";
        constexpr StringView kResetLine = R"({"op":"reset","params":{"seed":7},"gap":{"frames":0,"seconds":0.0}})";

        String Lines(std::initializer_list<StringView> lines) {
            String text;
            for (const StringView line : lines) {
                AppendFormat(text, "{}\n", line);
            }
            return text;
        }

        /// The error of a text that has to be refused
        ReplayError Refused(StringView text) {
            const auto parsed = ParseReplay(text);
            EXPECT_FALSE(parsed.has_value()) << "the text was accepted";
            return parsed ? ReplayError{} : parsed.error();
        }

        ReplayEntry MakeEntry(ReplayOp op, StringView params, Optional<StringView> outcome, uint64_t frames, double seconds) {
            ReplayEntry entry;
            entry.op = op;
            entry.params = *ParseJson(params);
            if (outcome) {
                entry.outcome = *ParseJson(*outcome);
            }
            entry.gap = ReplayGap{frames, seconds};
            return entry;
        }

    }  // namespace

    TEST(ReplayFileTest, ARecordedSessionReadsBackAsWritten) {
        const ReplayHeader header{"Dice", "0.1.5", "a short game"};
        const Vector<ReplayEntry> entries = {
            MakeEntry(ReplayOp::Reset, R"({"seed":42})", std::nullopt, 0, 0.0),
            MakeEntry(ReplayOp::Action, R"({"type":"roll"})", StringView(R"({"value":4})"), 3, 0.05),
            MakeEntry(ReplayOp::State, R"({"counter":9})", std::nullopt, 120, 2.0),
        };
        String text;
        AppendFormat(text, "{}\n", FormatReplayHeader(header));
        for (const ReplayEntry& entry : entries) {
            AppendFormat(text, "{}\n", FormatReplayEntry(entry));
        }

        const auto parsed = ParseReplay(text);

        ASSERT_TRUE(parsed.has_value()) << DescribeReplayError(parsed.error()).c_str();
        EXPECT_EQ(parsed->header.game, header.game);
        EXPECT_EQ(parsed->header.engine, header.engine);
        EXPECT_EQ(parsed->header.title, header.title);
        ASSERT_EQ(parsed->entries.size(), entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            EXPECT_EQ(parsed->entries[i].op, entries[i].op) << i;
            EXPECT_EQ(parsed->entries[i].params, entries[i].params) << i;
            EXPECT_EQ(parsed->entries[i].outcome, entries[i].outcome) << i;
            EXPECT_EQ(parsed->entries[i].gap, entries[i].gap) << i;
            EXPECT_EQ(parsed->entries[i].line, i + 2) << i;
        }
    }

    TEST(ReplayFileTest, LinesAreWrittenInAFixedOrderWithoutANewline) {
        const ReplayEntry entry = MakeEntry(ReplayOp::Action, R"({"type":"roll","a":1})", StringView(R"({"value":4})"), 3, 0.5);

        EXPECT_EQ(FormatReplayEntry(entry), R"({"op":"action","params":{"a":1,"type":"roll"},"outcome":{"value":4},"gap":{"frames":3,"seconds":0.5}})");
        EXPECT_EQ(FormatReplayHeader(ReplayHeader{"Dice", "0.1.5", ""}), kHeaderLine);
        EXPECT_EQ(FormatReplayHeader(ReplayHeader{"Dice", "0.1.5", "x"}), R"({"replay":1,"game":"Dice","engine":"0.1.5","title":"x"})");
    }

    TEST(ReplayFileTest, ATextWithNewlinesAndQuotesStaysOnOneLine) {
        const ReplayHeader header{"Di\"ce", "0.1.5", "two\nlines"};

        const String line = FormatReplayHeader(header);

        EXPECT_EQ(line.find('\n'), String::npos);
        const auto parsed = ParseReplay(Lines({StringView(line.c_str(), line.size()), kResetLine}));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed->header.game, header.game);
        EXPECT_EQ(parsed->header.title, header.title);
    }

    TEST(ReplayFileTest, BlankLinesAreSkippedAndKeepTheirNumbers) {
        const auto parsed = ParseReplay(Lines({"", kHeaderLine, "   ", kResetLine, ""}));

        ASSERT_TRUE(parsed.has_value());
        ASSERT_EQ(parsed->entries.size(), 1u);
        EXPECT_EQ(parsed->entries[0].line, 4u);
    }

    TEST(ReplayFileTest, AFileFromWindowsAndWithoutAFinalNewlineReads) {
        String text;
        AppendFormat(text, "{}\r\n{}", kHeaderLine, kResetLine);

        const auto parsed = ParseReplay(text);

        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed->entries.size(), 1u);
    }

    TEST(ReplayFileTest, AFileWithOnlyAHeaderHasNoEntries) {
        const auto parsed = ParseReplay(Lines({kHeaderLine}));

        ASSERT_TRUE(parsed.has_value());
        EXPECT_TRUE(parsed->entries.empty());
    }

    TEST(ReplayFileTest, AnUnknownVersionIsRefusedAtLineOne) {
        const ReplayError error = Refused(Lines({R"({"replay":2,"game":"Dice","engine":"9.9.9"})", kResetLine}));

        EXPECT_EQ(error.line, 1u);
        EXPECT_NE(error.message.find("unknown replay version 2"), String::npos) << error.message.c_str();
    }

    TEST(ReplayFileTest, AnEmptyFileAndAFileWithoutAHeaderAreRefused) {
        EXPECT_EQ(Refused("").line, 1u);
        EXPECT_EQ(Refused("\n\n").line, 1u);
        const ReplayError error = Refused(Lines({kResetLine}));
        EXPECT_EQ(error.line, 1u);
        EXPECT_NE(error.message.find("header"), String::npos) << error.message.c_str();
    }

    TEST(ReplayFileTest, EveryBadHeaderNamesItsLine) {
        EXPECT_EQ(Refused(Lines({"", R"({"replay":"1","game":"Dice","engine":"0"})"})).line, 2u);
        EXPECT_EQ(Refused(Lines({R"({"replay":1,"engine":"0"})"})).line, 1u);
        EXPECT_EQ(Refused(Lines({R"({"replay":1,"game":"Dice"})"})).line, 1u);
        EXPECT_EQ(Refused(Lines({R"({"replay":1,"game":"Dice","engine":"0","title":5})"})).line, 1u);
        EXPECT_EQ(Refused(Lines({R"({"replay":1,"game":"Dice","engine":"0","extra":5})"})).line, 1u);
    }

    TEST(ReplayFileTest, ALineThatIsNotAnObjectIsRefused) {
        EXPECT_EQ(Refused(Lines({kHeaderLine, "[1,2]"})).line, 2u);
        EXPECT_EQ(Refused(Lines({kHeaderLine, "7"})).line, 2u);
        EXPECT_EQ(Refused(Lines({kHeaderLine, kResetLine, "{not json"})).line, 3u);
        EXPECT_EQ(Refused(Lines({"[]"})).line, 1u);
    }

    TEST(ReplayFileTest, AnUnknownOpIsRefusedWithItsName) {
        const ReplayError error = Refused(Lines({kHeaderLine, kResetLine, R"({"op":"undo","params":{},"gap":{"frames":0,"seconds":0}})"}));

        EXPECT_EQ(error.line, 3u);
        EXPECT_NE(error.message.find("undo"), String::npos) << error.message.c_str();
    }

    TEST(ReplayFileTest, AMissingOrMistypedMemberIsRefusedAtItsLine) {
        constexpr StringView kBadEntries[] = {
            R"({"params":{},"gap":{"frames":0,"seconds":0}})",
            R"({"op":1,"params":{},"gap":{"frames":0,"seconds":0}})",
            R"({"op":"action","gap":{"frames":0,"seconds":0}})",
            R"({"op":"action","params":[],"gap":{"frames":0,"seconds":0}})",
            R"({"op":"action","params":{}})",
            R"({"op":"action","params":{},"gap":5})",
            R"({"op":"action","params":{},"gap":{"seconds":0}})",
            R"({"op":"action","params":{},"gap":{"frames":1.5,"seconds":0}})",
            R"({"op":"action","params":{},"gap":{"frames":"1","seconds":0}})",
            R"({"op":"action","params":{},"gap":{"frames":1}})",
            R"({"op":"action","params":{},"gap":{"frames":1,"seconds":"0"}})",
            R"({"op":"action","params":{},"gap":{"frames":0,"seconds":0},"note":"x"})",
        };
        for (const StringView bad : kBadEntries) {
            const ReplayError error = Refused(Lines({kHeaderLine, kResetLine, bad}));
            EXPECT_EQ(error.line, 3u) << String(bad).c_str();
        }
    }

    TEST(ReplayFileTest, ANegativeOrImpossibleGapIsRefused) {
        constexpr StringView kBadGaps[] = {
            R"({"frames":-1,"seconds":0})",
            R"({"frames":1,"seconds":-0.5})",
            R"({"frames":0,"seconds":0.5})",
            R"({"frames":1,"seconds":1e999})",
        };
        for (const StringView gap : kBadGaps) {
            String entry;
            AppendFormat(entry, R"({{"op":"action","params":{{}},"gap":{}}})", gap);
            const auto parsed = ParseReplay(Lines({kHeaderLine, kResetLine, StringView(entry.c_str(), entry.size())}));
            ASSERT_FALSE(parsed.has_value()) << String(gap).c_str();
            EXPECT_EQ(parsed.error().line, 3u) << String(gap).c_str();
        }
    }

    TEST(ReplayFileTest, TheFirstEntryHasToBeAReset) {
        const ReplayError error = Refused(Lines({kHeaderLine, R"({"op":"action","params":{"type":"roll"},"gap":{"frames":0,"seconds":0}})", kResetLine}));

        EXPECT_EQ(error.line, 2u);
        EXPECT_NE(error.message.find("reset"), String::npos) << error.message.c_str();
    }

    TEST(ReplayFileTest, ALineOverTheRequestLimitIsRefused) {
        String params(ControlService::kMaxRequestBytes, 'a');
        String entry;
        AppendFormat(entry, R"({{"op":"reset","params":{{"x":"{}"}},"gap":{{"frames":0,"seconds":0}}}})", params);

        const ReplayError error = Refused(Lines({kHeaderLine, StringView(entry.c_str(), entry.size())}));

        EXPECT_EQ(error.line, 2u);
    }

    TEST(ReplayFileTest, ALineNestedTooDeepIsRefusedWithoutRecursing) {
        String deep;
        for (size_t i = 0; i < ControlService::kMaxRequestDepth + 10; ++i) {
            deep.push_back('[');
        }
        const ReplayError error = Refused(Lines({kHeaderLine, StringView(deep.c_str(), deep.size())}));

        EXPECT_EQ(error.line, 2u);
    }

    TEST(ReplayFileTest, EveryOpHasAWireName) {
        for (const ReplayOp op : {ReplayOp::Reset, ReplayOp::Action, ReplayOp::State}) {
            EXPECT_EQ(FindReplayOp(ReplayOpName(op)), op);
        }
        EXPECT_FALSE(FindReplayOp("Reset").has_value());
        EXPECT_EQ(DescribeReplayError(ReplayError{3, "x"}), "line 3: x");
    }

}  // namespace BECore::Tests
