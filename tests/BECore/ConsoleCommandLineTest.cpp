#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Widgets/Console/ConsoleCommandLine.h>
#include <Widgets/Console/ConsoleSession.h>
#include <Widgets/EditorLayout/EditorSettings.h>

namespace BECore::Tests {

    namespace {

        /// "method {params}" of a command that parsed, "error: message" of one that did not
        String Parsed(StringView text) {
            const auto command = ParseConsoleCommand(text);
            if (!command) {
                return Format("error: {}", command.error());
            }
            return Format("{} {}", command->method, DumpJson(command->params));
        }

        char KindLetter(ConsoleOutputKind kind) {
            switch (kind) {
                case ConsoleOutputKind::Input:
                    return 'I';
                case ConsoleOutputKind::Result:
                    return 'R';
                case ConsoleOutputKind::Error:
                    return 'E';
                default:
                    return 'N';
            }
        }

        /// The lines of an output as `K:text`, joined by '|'
        String Lines(const ConsoleOutput& output) {
            String text;
            for (size_t i = 0; i < output.Size(); ++i) {
                if (i != 0) {
                    text += "|";
                }
                text += KindLetter(output.At(i).kind);
                text += ":";
                text += output.At(i).text;
            }
            return text;
        }

        class ConsoleCommandLineTest : public ::testing::Test {
        protected:
            void SetUp() override {
                service.RegisterHandler("ping", [](ControlCall& call) { call.Reply(Json::object()); });
                service.RegisterHandler("status", [](ControlCall& call) { call.Reply(Json::object()); });
                service.RegisterHandler("step", [](ControlCall& call) { call.Reply(Json::object()); });
                service.DescribeMethod("step", {"Runs frames", {{"frames", "integer", false, ""}, {"dt", "number", false, ""}}});
                service.RegisterHandler("echo", [this](ControlCall& call) {
                    ++echoCalls;
                    lastParams = DumpJson(call.Params());
                    Json result = Json::object();
                    result["params"] = call.Params();
                    call.Reply(std::move(result));
                });
                service.DescribeMethod("echo", {"Answers its parameters", {{"from", "string", true, ""}, {"to", "string", true, ""}, {"flag", "boolean", false, ""}, {"format", "string", false, ""}}});
                service.RegisterHandler("fail", [](ControlCall& call) { call.Fail(ControlErrors::kInvalidParams, "bad frames"); });
                service.RegisterHandler("later", [](ControlCall& call) {
                    call.DeferFrames(1, [](ControlCall& done) {
                        Json result = Json::object();
                        result["done"] = true;
                        done.Reply(std::move(result));
                    });
                });
                service.RegisterHandler("big", [](ControlCall& call) {
                    Json result = Json::object();
                    result["text"] = std::string(10000, 'a');  //-NoStd: nlohmann takes std::string
                    call.Reply(std::move(result));
                });
            }

            /// The line after Tab, then the candidates, joined by '|' (the text alone when there are none)
            String Complete(StringView text) {
                const Vector<ControlMethodListing> methods = service.GetMethods();
                const ConsoleCompletion completion = CompleteConsoleCommand(text, methods);
                String joined = Format("[{}]", completion.text);
                for (const String& candidate : completion.candidates) {
                    joined += Format("|{}", candidate);
                }
                return joined;
            }

            ControlService service;
            ConsoleSession session;
            int echoCalls = 0;
            String lastParams;
        };

    }  // namespace

    TEST_F(ConsoleCommandLineTest, AMethodAloneHasNoParameters) {
        EXPECT_EQ(Parsed("ping"), "ping {}");
        EXPECT_EQ(Parsed("   ping   "), "ping {}");
        EXPECT_EQ(Parsed("a.b_c1"), "a.b_c1 {}");
    }

    TEST_F(ConsoleCommandLineTest, TheRestIsOneJsonObjectWhenItStartsWithABrace) {
        EXPECT_EQ(Parsed("echo {\"a\":1}"), "echo {\"a\":1}");
        EXPECT_EQ(Parsed("echo  {\"a\": [1, 2], \"b\": {\"c\": null}}  "), "echo {\"a\":[1,2],\"b\":{\"c\":null}}");
        EXPECT_EQ(Parsed("echo {}"), "echo {}");
    }

    TEST_F(ConsoleCommandLineTest, KeyValueWordsReadJsonValuesAndKeepTheRestAsText) {
        EXPECT_EQ(Parsed("echo a=1 b=\"x y\" c=[1,2] d=true e=hello"), "echo {\"a\":1,\"b\":\"x y\",\"c\":[1,2],\"d\":true,\"e\":\"hello\"}");
        EXPECT_EQ(Parsed("perform_action move=e2e4"), "perform_action {\"move\":\"e2e4\"}");
        EXPECT_EQ(Parsed("echo a={\"x\": 1, \"y\": [2, 3]} b=2"), "echo {\"a\":{\"x\":1,\"y\":[2,3]},\"b\":2}");
        EXPECT_EQ(Parsed("echo name=\"a=b\""), "echo {\"name\":\"a=b\"}");
        EXPECT_EQ(Parsed("echo a=1.5 b=-2 c="), "echo {\"a\":1.5,\"b\":-2,\"c\":\"\"}");
    }

    TEST_F(ConsoleCommandLineTest, AnythingElseThanAnObjectIsRefused) {
        EXPECT_TRUE(Contains(Parsed("echo [1,2]"), "error:"));
        EXPECT_TRUE(Contains(Parsed("echo \"text\""), "error:"));
        EXPECT_TRUE(Contains(Parsed("echo 5"), "key=value"));
    }

    TEST_F(ConsoleCommandLineTest, BadTextIsAnErrorAndNeverAborts) {
        EXPECT_TRUE(Contains(Parsed(""), "error:"));
        EXPECT_TRUE(Contains(Parsed("   "), "error:"));
        EXPECT_TRUE(Contains(Parsed("echo {\"a\":1"), "error:"));
        EXPECT_TRUE(Contains(Parsed("echo {\"a\":1} extra"), "error:"));
        EXPECT_TRUE(Contains(Parsed("echo a"), "key=value"));
        EXPECT_TRUE(Contains(Parsed("echo =1"), "no key"));
        EXPECT_TRUE(Contains(Parsed("echo a=1 a=2"), "twice"));
        EXPECT_TRUE(Contains(Parsed("echo a={1"), "error:"));
        EXPECT_TRUE(Contains(Parsed("echo a=1}"), "unbalanced"));
        EXPECT_TRUE(Contains(Parsed("echo a=[1}"), "unbalanced"));
        EXPECT_TRUE(Contains(Parsed("echo a=\"x"), "not closed"));
        EXPECT_TRUE(Contains(Parsed("echo a=null"), "null"));
        EXPECT_TRUE(Contains(Parsed("echo a b=1"), "key=value"));
        EXPECT_TRUE(Contains(Parsed("echo \"a b\"=1"), "error:"));
    }

    TEST_F(ConsoleCommandLineTest, ANestingTheControlApiWouldRefuseIsAnError) {
        const size_t depth = ControlService::kMaxRequestDepth + 10;
        const String deep = Format("{}{}", String(depth, '['), String(depth, ']'));
        EXPECT_TRUE(Contains(Parsed(Format("echo a={}", deep)), "nested too deep"));
        EXPECT_TRUE(Contains(Parsed(Format("echo {{\"a\":{}}}", deep)), "nested too deep"));
        EXPECT_TRUE(Contains(Parsed(Format("echo a={}", String(5000, '{'))), "error:"));

        const size_t allowed = ControlService::kMaxRequestDepth - 2;
        EXPECT_FALSE(Contains(Parsed(Format("echo a={}{}", String(allowed, '['), String(allowed, ']'))), "error:"));
    }

    TEST_F(ConsoleCommandLineTest, AMethodNameHasOnlyNameCharacters) {
        EXPECT_TRUE(Contains(Parsed("ec#ho"), "not a method name"));
        EXPECT_TRUE(Contains(Parsed("echo{\"a\":1}"), "not a method name"));
        EXPECT_TRUE(Contains(Parsed("echo/x a=1"), "not a method name"));
    }

    TEST_F(ConsoleCommandLineTest, ATextOfTheKeyIsCheckedToo) {
        EXPECT_TRUE(Contains(Parsed("echo a;b=1"), "not a parameter name"));
    }

    TEST_F(ConsoleCommandLineTest, AUniquePrefixOfAMethodIsCompletedWithABlank) {
        EXPECT_EQ(Complete("pi"), "[ping ]");
        EXPECT_EQ(Complete("ste"), "[step ]");
        EXPECT_EQ(Complete("  ste"), "[  step ]");
        EXPECT_EQ(Complete("step"), "[step ]");
    }

    TEST_F(ConsoleCommandLineTest, SeveralMethodsCompleteToTheCommonPrefixAndAreListed) {
        EXPECT_EQ(Complete("st"), "[st]|status|step");
        EXPECT_EQ(Complete("e"), "[echo ]");
        EXPECT_EQ(Complete(""), "[]|big|echo|fail|later|ping|status|step");
    }

    TEST_F(ConsoleCommandLineTest, NoMatchChangesNothing) {
        EXPECT_EQ(Complete("zz"), "[zz]");
        EXPECT_EQ(Complete("ping a"), "[ping a]");
        EXPECT_EQ(Complete("nosuch f"), "[nosuch f]");
    }

    TEST_F(ConsoleCommandLineTest, ParameterNamesAreOfferedAfterTheMethod) {
        EXPECT_EQ(Complete("step f"), "[step frames=]");
        EXPECT_EQ(Complete("step "), "[step ]|frames (integer)|dt (number)");
        EXPECT_EQ(Complete("step d"), "[step dt=]");
    }

    TEST_F(ConsoleCommandLineTest, TheRequiredParametersAreListedFirstWithTheirType) {
        EXPECT_EQ(Complete("echo "), "[echo ]|from (string, required)|to (string, required)|flag (boolean)|format (string)");
        EXPECT_EQ(Complete("echo f"), "[echo f]|from (string, required)|flag (boolean)|format (string)");
        EXPECT_EQ(Complete("echo fo"), "[echo format=]");
        EXPECT_EQ(Complete("echo fr"), "[echo from=]");
        EXPECT_EQ(Complete("echo fl"), "[echo flag=]");
    }

    TEST_F(ConsoleCommandLineTest, AParameterAlreadyGivenIsNotOfferedAgain) {
        EXPECT_EQ(Complete("step frames=3 "), "[step frames=3 dt=]");
        EXPECT_EQ(Complete("echo from=x t"), "[echo from=x to=]");
        EXPECT_EQ(Complete("echo to=x t"), "[echo to=x t]");
        EXPECT_EQ(Complete("echo from=x to=y flag=true f"), "[echo from=x to=y flag=true format=]");
    }

    TEST_F(ConsoleCommandLineTest, AFinishedValueIsNotExtended) {
        EXPECT_EQ(Complete("step frames=3"), "[step frames=3]");
        EXPECT_EQ(Complete("echo from=x"), "[echo from=x]");
    }

    TEST_F(ConsoleCommandLineTest, TheJsonFormAndAnOpenQuoteOfferNothing) {
        EXPECT_EQ(Complete("echo {\"f"), "[echo {\"f]");
        EXPECT_EQ(Complete("echo  {"), "[echo  {]");
        EXPECT_EQ(Complete("echo from=\"a b"), "[echo from=\"a b]");
        EXPECT_EQ(Complete("echo from=[1, f"), "[echo from=[1, f]");
    }

    TEST_F(ConsoleCommandLineTest, ASubmittedCommandReachesTheHandlerAndItsAnswerIsPrinted) {
        const Optional<String> kept = session.Submit(service, "  echo a=1 b=x ");
        ASSERT_TRUE(kept.has_value());
        EXPECT_EQ(*kept, "echo a=1 b=x");
        EXPECT_EQ(Lines(session.GetOutput()), "I:echo a=1 b=x");

        service.Drain();
        EXPECT_EQ(echoCalls, 1);
        EXPECT_EQ(lastParams, "{\"a\":1,\"b\":\"x\"}");
        EXPECT_EQ(Lines(session.GetOutput()), "I:echo a=1 b=x|R:{\"params\":{\"a\":1,\"b\":\"x\"}}");
    }

    TEST_F(ConsoleCommandLineTest, AnErrorOfTheHandlerIsPrintedAsCodeAndMessage) {
        EXPECT_TRUE(session.Submit(service, "fail").has_value());
        service.Drain();
        EXPECT_EQ(Lines(session.GetOutput()), "I:fail|E:invalid_params: bad frames");
        EXPECT_EQ(session.GetHistory().GetEntries().size(), 1u);
    }

    TEST_F(ConsoleCommandLineTest, AnUnknownMethodIsPrintedLikeAnyError) {
        EXPECT_TRUE(session.Submit(service, "nosuch").has_value());
        service.Drain();
        EXPECT_EQ(session.GetOutput().Size(), 2u);
        EXPECT_TRUE(Contains(session.GetOutput().At(1).text, "unknown_method:"));
        EXPECT_EQ(session.GetOutput().At(1).kind, ConsoleOutputKind::Error);
    }

    TEST_F(ConsoleCommandLineTest, ALineThatDoesNotParsePostsNothingAndIsNotKept) {
        EXPECT_FALSE(session.Submit(service, "echo a").has_value());
        service.Drain();
        EXPECT_EQ(echoCalls, 0);
        EXPECT_EQ(session.GetOutput().Size(), 2u);
        EXPECT_EQ(session.GetOutput().At(0).kind, ConsoleOutputKind::Input);
        EXPECT_EQ(session.GetOutput().At(1).kind, ConsoleOutputKind::Error);
        EXPECT_TRUE(session.GetHistory().GetEntries().empty());
    }

    TEST_F(ConsoleCommandLineTest, AnEmptyLineDoesNothing) {
        EXPECT_FALSE(session.Submit(service, "").has_value());
        EXPECT_FALSE(session.Submit(service, "  \t ").has_value());
        service.Drain();
        EXPECT_EQ(session.GetOutput().Size(), 0u);
    }

    TEST_F(ConsoleCommandLineTest, ALineLongerThanTheLimitIsRefused) {
        const String line = Format("echo a={}", String(kMaxConsoleCommandLength, 'x'));
        EXPECT_FALSE(session.Submit(service, line).has_value());
        service.Drain();
        EXPECT_EQ(echoCalls, 0);
        EXPECT_EQ(session.GetOutput().At(1).kind, ConsoleOutputKind::Error);
    }

    TEST_F(ConsoleCommandLineTest, ADeferredAnswerAppearsWhenTheFrameEnds) {
        EXPECT_TRUE(session.Submit(service, "later").has_value());
        service.Drain();
        EXPECT_EQ(Lines(session.GetOutput()), "I:later");

        service.EndFrame();
        EXPECT_EQ(Lines(session.GetOutput()), "I:later|R:{\"done\":true}");
    }

    TEST_F(ConsoleCommandLineTest, ADeferredAnswerAfterTheSessionIsGoneOnlyWritesTheSharedOutput) {
        auto gone = MakeUnique<ConsoleSession>();
        const IntrusivePtrAtomic<ConsoleOutput> output(&gone->GetOutput());
        EXPECT_TRUE(gone->Submit(service, "later").has_value());
        service.Drain();
        gone.reset();

        service.EndFrame();
        EXPECT_EQ(Lines(*output), "I:later|R:{\"done\":true}");
    }

    TEST_F(ConsoleCommandLineTest, TheOutputKeepsTheNewestLinesOnly) {
        ConsoleOutput& output = session.GetOutput();
        const size_t count = ConsoleOutput::kMaxLines + 50;
        for (size_t i = 0; i < count; ++i) {
            output.Append(ConsoleOutputKind::Note, ToString(i));
        }
        ASSERT_EQ(output.Size(), ConsoleOutput::kMaxLines);
        EXPECT_EQ(output.At(0).text, ToString(count - ConsoleOutput::kMaxLines));
        EXPECT_EQ(output.At(ConsoleOutput::kMaxLines - 1).text, ToString(count - 1));

        const uint64_t version = output.GetVersion();
        output.Clear();
        EXPECT_EQ(output.Size(), 0u);
        EXPECT_GT(output.GetVersion(), version);
    }

    TEST_F(ConsoleCommandLineTest, ALongAnswerIsCutAndSaysHowMuchWasLeftOut) {
        EXPECT_TRUE(session.Submit(service, "big").has_value());
        service.Drain();
        ASSERT_EQ(session.GetOutput().Size(), 2u);
        const String& text = session.GetOutput().At(1).text;
        EXPECT_LT(text.size(), ConsoleOutput::kMaxLineBytes + 64);
        EXPECT_TRUE(Contains(text, "more bytes)"));
        EXPECT_EQ(text.find("... ("), ConsoleOutput::kMaxLineBytes);
    }

    TEST_F(ConsoleCommandLineTest, ACutNeverSplitsACharacter) {
        String text(ConsoleOutput::kMaxLineBytes - 1, 'a');
        text += "\xC3\xA9";
        text += "tail";
        ConsoleOutput& output = session.GetOutput();
        output.Append(ConsoleOutputKind::Note, text);
        EXPECT_EQ(output.At(0).text.find("... ("), ConsoleOutput::kMaxLineBytes - 1);
    }

    TEST_F(ConsoleCommandLineTest, TheHistoryIsBoundedAndAnEqualCommandMovesToTheEnd) {
        ConsoleCommandHistory history;
        for (size_t i = 0; i < kMaxConsoleHistory + 6; ++i) {
            history.Add(ToString(i));
        }
        ASSERT_EQ(history.GetEntries().size(), kMaxConsoleHistory);
        EXPECT_EQ(history.GetEntries().front(), "6");
        EXPECT_EQ(history.GetEntries().back(), ToString(kMaxConsoleHistory + 5));

        history.Add("6");
        EXPECT_EQ(history.GetEntries().size(), kMaxConsoleHistory);
        EXPECT_EQ(history.GetEntries().front(), "7");
        EXPECT_EQ(history.GetEntries().back(), "6");

        history.Add("");
        EXPECT_EQ(history.GetEntries().size(), kMaxConsoleHistory);
    }

    TEST_F(ConsoleCommandLineTest, UpAndDownWalkTheHistoryAndGiveTheDraftBack) {
        ConsoleCommandHistory history;
        history.Set({"a", "b", "c"});

        EXPECT_EQ(history.Previous("dra"), StringView("c"));
        EXPECT_EQ(history.Previous("ignored"), StringView("b"));
        EXPECT_EQ(history.Previous(""), StringView("a"));
        EXPECT_EQ(history.Previous(""), StringView("a"));
        EXPECT_EQ(history.Next(), StringView("b"));
        EXPECT_EQ(history.Next(), StringView("c"));
        EXPECT_EQ(history.Next(), StringView("dra"));
        EXPECT_FALSE(history.Next().has_value());
    }

    TEST_F(ConsoleCommandLineTest, DownWithoutAWalkAndUpWithoutEntriesDoNothing) {
        ConsoleCommandHistory history;
        EXPECT_FALSE(history.Previous("x").has_value());
        EXPECT_FALSE(history.Next().has_value());
    }

    TEST_F(ConsoleCommandLineTest, AddingACommandEndsTheWalk) {
        ConsoleCommandHistory history;
        history.Set({"a", "b"});
        EXPECT_EQ(history.Previous("d"), StringView("b"));
        history.Add("c");
        EXPECT_FALSE(history.Next().has_value());
        EXPECT_EQ(history.Previous(""), StringView("c"));
    }

    TEST_F(ConsoleCommandLineTest, TheStoredStartCleansAndBoundsTheEntries) {
        ConsoleCommandHistory history;
        history.Set({"x", "", "y", "x"});
        ASSERT_EQ(history.GetEntries().size(), 2u);
        EXPECT_EQ(history.GetEntries()[0], "y");
        EXPECT_EQ(history.GetEntries()[1], "x");

        Vector<String> many;
        for (size_t i = 0; i < kMaxConsoleHistory * 2; ++i) {
            many.push_back(ToString(i));
        }
        history.Set(many);
        ASSERT_EQ(history.GetEntries().size(), kMaxConsoleHistory);
        EXPECT_EQ(history.GetEntries().front(), ToString(kMaxConsoleHistory));
    }

    TEST_F(ConsoleCommandLineTest, TheSessionKeepsTheLinesThatParsed) {
        EXPECT_TRUE(session.Submit(service, "ping").has_value());
        EXPECT_TRUE(session.Submit(service, "echo a=1").has_value());
        EXPECT_FALSE(session.Submit(service, "echo a").has_value());
        EXPECT_TRUE(session.Submit(service, "ping").has_value());
        ASSERT_EQ(session.GetHistory().GetEntries().size(), 2u);
        EXPECT_EQ(session.GetHistory().GetEntries()[0], "echo a=1");
        EXPECT_EQ(session.GetHistory().GetEntries()[1], "ping");
    }

}  // namespace BECore::Tests
