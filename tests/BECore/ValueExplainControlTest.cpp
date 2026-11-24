// The control method explain_value (Values/Explain/ValueExplainControl.h): the answer for each kind of value, the contexts a game registers and every refusal a client can provoke.

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ReflectionControl.h>
#include <BECore/ProgramListTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Explain/ProgramTrace.h>
#include <BECore/Values/Explain/ValueContextRegistry.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <Common/ControlTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramListData;
    using namespace ValueTestData;

    namespace {

        constexpr StringView kBattle = "battle";
        constexpr StringView kElsewhere = "elsewhere_context_4c9a";
        constexpr size_t kLongList = 600;
        constexpr size_t kHostileLength = 10000;

        class ValueExplainControlTest : public ::testing::Test {
        protected:
            void SetUp() override {
                ValueContextRegistry::GetInstance().Clear();
                RegisterControlBuiltins(service);
                service.Roots().Register("doc", ControlRoot{[this] { return TypedRef::Of(document); }});
                service.Roots().Register("behind", ControlRoot{[this] { return TypedRef::Of(behind); }});
                service.Roots().Register("empty", ControlRoot{[] { return TypedRef{}; }});

                const auto damage = CompileValue<int32_t>("caster.attack * 2 + cast.level", world.declaration);
                ASSERT_TRUE(damage.has_value());
                document.damage = *damage;
                ASSERT_TRUE(document.aim.Link(world.declaration, "caster.velocity").has_value());
            }

            void TearDown() override {
                ValueContextRegistry::GetInstance().Clear();
            }

            void RegisterBattle(StringView name = kBattle) {
                ValueContextRegistry::GetInstance().Register(name, [this]() -> Optional<ExplainContext> { return ExplainContext{&world.declaration, world.context}; });
            }

            static String ExplainParams(StringView root, StringView path, Optional<StringView> context = std::nullopt) {
                String params;
                JsonWriter writer(params);
                writer.BeginObject();
                writer.Member("root", root);
                writer.Member("path", path);
                if (context.has_value()) {
                    writer.Member("context", *context);
                }
                writer.EndObject();
                return params;
            }

            ControlReply Explain(StringView root, StringView path, Optional<StringView> context = std::nullopt) {
                const String params = ExplainParams(root, path, context);
                return CallControlText(service, ++nextId, "explain_value", StringView(params.c_str(), params.size()));
            }

            /// The reply text of `explain_value` for @p formula over a hand of @p length tiles, as the service wrote it
            String ExplainLoopText(StringView formula, size_t length) {
                ContextDecl declaration;
                EXPECT_TRUE(declaration.Add<ProgramListHand>("hand").has_value());
                ProgramListHand hand;
                hand.none.resize(length);
                const auto total = CompileValue<int32_t>(formula, declaration);
                EXPECT_TRUE(total.has_value());
                if (!total) {
                    return String();
                }
                document.damage = *total;
                Context context(declaration);
                context.Set(0, hand);
                ValueContextRegistry::GetInstance().Register(kBattle, [&]() -> Optional<ExplainContext> { return ExplainContext{&declaration, context}; });
                const String params = ExplainParams("doc", "damage", kBattle);
                String reply;
                service.Post(MakeRequestText(++nextId, "explain_value", StringView(params.c_str(), params.size())), [&reply](String text) { reply = std::move(text); });
                service.Drain();
                return reply;
            }

            ControlService service;
            ValueWorld world;
            ValueDocument document;
            ValueBehind behind;
            int64_t nextId = 0;
        };

        void CollectLabels(JsonView node, Vector<String>& labels) {
            labels.push_back(StringOf(node, "label"));
            const JsonView children = MemberOf(node, "children");
            for (const JsonView child : children.Items()) {
                CollectLabels(child, labels);
            }
        }

        size_t CountNodes(JsonView node) {
            size_t count = 1;
            for (const JsonView child : MemberOf(node, "children").Items()) {
                count += CountNodes(child);
            }
            return count;
        }

        /// How many containers the deepest value of @p json is inside
        size_t ContainerDepth(JsonView json) {
            size_t deepest = 0;
            if (json.IsArray()) {
                for (const JsonView child : json.Items()) {
                    deepest = std::max(deepest, ContainerDepth(child));
                }
                return deepest + 1;
            }
            if (json.IsObject()) {
                for (const JsonMember member : json.Members()) {
                    deepest = std::max(deepest, ContainerDepth(member.value));
                }
                return deepest + 1;
            }
            return 0;
        }

        bool Contains(const Vector<String>& labels, StringView label) {
            return std::find(labels.begin(), labels.end(), label) != labels.end();
        }

    }  // namespace

    TEST_F(ValueExplainControlTest, AProgramAnswersItsValueAndTheTraceOfHowItWasComputed) {
        RegisterBattle();
        const ControlReply response = Explain("doc", "damage", kBattle);
        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage().c_str();
        const JsonView result = response.Result();
        EXPECT_EQ(StringOf(result, "kind"), "Program");
        EXPECT_NE(StringOf(result, "source").find("caster.attack"), String::npos);
        EXPECT_EQ(BoolOf(result, "has_value"), true);
        EXPECT_EQ(StringOf(result, "value"), "27");

        const JsonView trace = MemberOf(result, "trace");
        ASSERT_TRUE(trace.IsObject());
        EXPECT_EQ(StringOf(trace, "source"), "Operation");
        EXPECT_EQ(StringOf(trace, "value"), "27");
        EXPECT_EQ(StringOf(trace, "type"), "Int32");
        Vector<String> labels;
        CollectLabels(trace, labels);
        EXPECT_TRUE(Contains(labels, "caster.attack"));
        EXPECT_TRUE(Contains(labels, "cast.level"));
    }

    TEST_F(ValueExplainControlTest, ASlotAnswersOneNodeAndAConstHasNoTrace) {
        RegisterBattle();
        const ControlReply slotReply = Explain("doc", "aim", kBattle);
        const JsonView slot = slotReply.Result();
        EXPECT_EQ(StringOf(slot, "kind"), "Slot");
        EXPECT_EQ(StringOf(slot, "source"), "caster.velocity");
        EXPECT_EQ(StringOf(slot, "value"), "(1, 2, 3)");
        const JsonView trace = MemberOf(slot, "trace");
        ASSERT_TRUE(trace.IsObject());
        EXPECT_EQ(StringOf(trace, "source"), "ContextKey");
        EXPECT_EQ(StringOf(trace, "label"), "caster.velocity");
        EXPECT_EQ(CountNodes(trace), 1U);

        const ControlReply constantReply = Explain("doc", "power", kBattle);
        const JsonView constant = constantReply.Result();
        EXPECT_EQ(StringOf(constant, "kind"), "Const");
        EXPECT_EQ(StringOf(constant, "value"), "1");
        EXPECT_FALSE(constant.Find("trace").has_value());
    }

    TEST_F(ValueExplainControlTest, TheAnswerFollowsTheObjectsNow) {
        RegisterBattle();
        world.caster.attack = 100;
        EXPECT_EQ(StringOf(Explain("doc", "damage", kBattle).Result(), "value"), "203");
    }

    TEST_F(ValueExplainControlTest, ARequestWithoutAContextUsesTheDefaultProviderAndWithoutOneAnswersOnlyKindAndSource) {
        const ControlReply bareReply = Explain("doc", "damage");
        const JsonView bare = bareReply.Result();
        EXPECT_EQ(StringOf(bare, "kind"), "Program");
        EXPECT_NE(StringOf(bare, "source").find("caster.attack"), String::npos);
        EXPECT_EQ(BoolOf(bare, "has_value"), false);
        EXPECT_NE(StringOf(bare, "why").find("no context"), String::npos);
        EXPECT_FALSE(bare.Find("value").has_value());
        EXPECT_FALSE(bare.Find("trace").has_value());

        const ControlReply constantReply = Explain("doc", "power");
        const JsonView constant = constantReply.Result();
        EXPECT_EQ(BoolOf(constant, "has_value"), false);
        EXPECT_EQ(StringOf(constant, "source"), "1");

        RegisterBattle(kDefaultContextName);
        const ControlReply servedReply = Explain("doc", "damage");
        const JsonView served = servedReply.Result();
        EXPECT_EQ(BoolOf(served, "has_value"), true);
        EXPECT_EQ(StringOf(served, "value"), "27");
    }

    TEST_F(ValueExplainControlTest, AContextWithoutAnObjectAtALevelTheValueReadsGivesNoValueAndSaysWhich) {
        ValueContextRegistry::GetInstance().Register(kBattle, [this]() -> Optional<ExplainContext> {
            Context partial(world.declaration);
            partial.Set(0, world.caster);
            return ExplainContext{&world.declaration, partial};
        });
        const ControlReply missingReply = Explain("doc", "damage", kBattle);
        const JsonView missing = missingReply.Result();
        EXPECT_EQ(BoolOf(missing, "has_value"), false);
        EXPECT_NE(StringOf(missing, "why").find("cast"), String::npos);
        EXPECT_FALSE(missing.Find("trace").has_value());
        // A value that reads only the level that is there still answers
        const ControlReply presentReply = Explain("doc", "aim", kBattle);
        const JsonView present = presentReply.Result();
        EXPECT_EQ(BoolOf(present, "has_value"), true);
    }

    TEST_F(ValueExplainControlTest, AProviderWithNoContextNowAnswersLikeNoContext) {
        ValueContextRegistry::GetInstance().Register(kBattle, []() -> Optional<ExplainContext> { return std::nullopt; });
        const ControlReply answerReply = Explain("doc", "damage", kBattle);
        const JsonView answer = answerReply.Result();
        EXPECT_EQ(BoolOf(answer, "has_value"), false);
        EXPECT_NE(StringOf(answer, "why").find(kBattle.data()), String::npos);
    }

    TEST_F(ValueExplainControlTest, ALongLoopIsCutAndTheAnswerStaysBounded) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ProgramListHand>("hand").has_value());
        ProgramListHand hand;
        hand.none.resize(kLongList);
        int32_t sum = 0;
        for (size_t i = 0; i < kLongList; ++i) {
            hand.none[i].rank = static_cast<int32_t>(i % 7);
            sum += hand.none[i].rank;
        }
        const auto total = CompileValue<int32_t>("sum(hand.none, t, t.rank)", declaration);
        ASSERT_TRUE(total.has_value());
        document.damage = *total;
        Context context(declaration);
        context.Set(0, hand);
        ValueContextRegistry::GetInstance().Register(kBattle, [&]() -> Optional<ExplainContext> { return ExplainContext{&declaration, context}; });

        const ControlReply reply = Explain("doc", "damage", kBattle);
        const JsonView result = reply.Result();
        EXPECT_EQ(StringOf(result, "value"), Format("{}", sum));
        EXPECT_EQ(BoolOf(result, "truncated"), true);
        const JsonView trace = MemberOf(result, "trace");
        ASSERT_TRUE(trace.IsObject());
        EXPECT_LE(CountNodes(trace), kMaxFormattedLines);
        EXPECT_LE(result.Raw().size(), kMaxFormattedLines * 400U);
    }

    TEST_F(ValueExplainControlTest, ATraceDeeperThanTheWriterIsCutToNullWhereTheWriterEnds) {
        constexpr size_t kLoopLength = 300;
        const String reply = ExplainLoopText("sum(hand.none, t, 1)", kLoopLength);

        const JsonDoc response(StringView(reply.c_str(), reply.size()));
        ASSERT_TRUE(response.IsParsed()) << reply.c_str();
        const JsonView result = MemberOf(response.Root(), "result");
        EXPECT_EQ(StringOf(result, "value"), "300");
        EXPECT_EQ(ContainerDepth(response.Root()), JsonWriter::kMaxDepth);
        EXPECT_NE(reply.find(R"("children":[null)"), String::npos);
    }

    TEST_F(ValueExplainControlTest, AnAnswerListsTheKeysInAlphabeticalOrderWithTruncatedAfterTheTrace) {
        const String reply = ExplainLoopText("sum(hand.none, t, t.rank)", kLongList);

        const size_t hasValue = reply.find(R"("has_value")");
        const size_t trace = reply.find(R"("trace")");
        const size_t truncated = reply.rfind(R"("truncated":true)");
        const size_t value = reply.rfind(R"("value":"0"})");
        ASSERT_NE(truncated, String::npos) << "a loop over " << kLongList << " tiles is cut";
        EXPECT_LT(hasValue, trace);
        EXPECT_LT(trace, truncated);
        EXPECT_LT(truncated, value);
    }

    TEST_F(ValueExplainControlTest, TheRefusalsHaveTheirOwnCodes) {
        RegisterBattle();
        const ControlReply noRoot = Explain("nothing", "damage", kBattle);
        EXPECT_FALSE(noRoot.IsOk());
        EXPECT_EQ(noRoot.ErrorCode(), "unknown_root");
        EXPECT_EQ(Explain("empty", "damage", kBattle).ErrorCode(), "no_object");

        const ControlReply notAValue = Explain("doc", "context", kBattle);
        EXPECT_EQ(notAValue.ErrorCode(), "not_a_value");
        EXPECT_NE(notAValue.ErrorMessage().find("Value<"), String::npos);

        EXPECT_EQ(Explain("doc", "damage", "elsewhere").ErrorCode(), "unknown_context");
        const ControlReply unknownContext = Explain("doc", "damage", "elsewhere");
        EXPECT_NE(unknownContext.ErrorMessage().find(kBattle.data()), String::npos);

        EXPECT_EQ(Explain("behind", "spans[0].low", kBattle).ErrorCode(), "not_fixed");
        EXPECT_EQ(Explain("doc", "missing_field", kBattle).ErrorCode(), "not_found");
    }

    TEST_F(ValueExplainControlTest, AMistypedRequestIsInvalidParams) {
        EXPECT_EQ(CallControlText(service, 1, "explain_value", R"({"path":5,"root":"doc"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(CallControlText(service, 2, "explain_value", R"({})").ErrorCode(), "invalid_params");
        EXPECT_EQ(CallControlText(service, 3, "explain_value", R"({"context":7,"path":"damage","root":"doc"})").ErrorCode(), "invalid_params");
    }

    TEST_F(ValueExplainControlTest, AHostilePathIsRefusedAndNeverAbortsAndInternsNothing) {
        RegisterBattle();
        const String hostile = "rc_explain_hostile_path_7e21d4";
        const Vector<String> paths = {"a..b", String(kHostileLength, '['), String(kHostileLength, 'a'), "damage.", "[", "damage[", ".damage", "damage]]", "", hostile};
        for (const String& path : paths) {
            const ControlReply response = Explain("doc", path, kBattle);
            EXPECT_FALSE(response.IsOk()) << path.substr(0, 20).c_str();
            EXPECT_FALSE(response.ErrorCode().empty());
        }
        EXPECT_FALSE(PoolString::Find(hostile).has_value());
        EXPECT_FALSE(PoolString::Find(kElsewhere).has_value());
        EXPECT_EQ(Explain("doc", "damage", kElsewhere).ErrorCode(), "unknown_context");
        EXPECT_FALSE(PoolString::Find(kElsewhere).has_value());
    }

    TEST_F(ValueExplainControlTest, TheMethodIsListedForClients) {
        const ControlReply listed = CallControlText(service, 1, "list_methods");
        const JsonView methods = MemberOf(listed.Result(), "methods");
        ASSERT_TRUE(methods.IsArray());
        bool found = false;
        for (const JsonView method : methods.Items()) {
            found = found || StringOf(method, "name") == "explain_value";
        }
        EXPECT_TRUE(found);
    }

}  // namespace BECore::Tests
