// The control method explain_value (Values/Explain/ValueExplainControl.h): the answer for each kind of value, the contexts a game registers and every refusal a client can provoke.

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/ReflectionControl.h>
#include <BECore/ProgramListTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Explain/ProgramTrace.h>
#include <BECore/Values/Explain/ValueContextRegistry.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <Common/ControlTestSupport.h>
#include <algorithm>
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

            Json Explain(StringView root, StringView path, Optional<StringView> context = std::nullopt) {
                Json params = Json::object();
                params["root"] = MakeJsonString(root);
                params["path"] = MakeJsonString(path);
                if (context.has_value()) {
                    params["context"] = MakeJsonString(*context);
                }
                return CallControl(service, ++nextId, "explain_value", params);
            }

            static const Json& Result(const Json& response) {
                const Json* result = FindJsonMember(response, "result");
                static const Json kNone = Json::object();
                return result != nullptr ? *result : kNone;
            }

            ControlService service;
            ValueWorld world;
            ValueDocument document;
            ValueBehind behind;
            int64_t nextId = 0;
        };

        void CollectLabels(const Json& node, Vector<String>& labels) {
            labels.push_back(StringOf(&node, "label"));
            const Json* children = FindJsonMember(node, "children");
            if (children == nullptr || !children->is_array()) {
                return;
            }
            for (const Json& child : *children) {
                CollectLabels(child, labels);
            }
        }

        size_t CountNodes(const Json& node) {
            size_t count = 1;
            const Json* children = FindJsonMember(node, "children");
            if (children != nullptr && children->is_array()) {
                for (const Json& child : *children) {
                    count += CountNodes(child);
                }
            }
            return count;
        }

        bool Contains(const Vector<String>& labels, StringView label) {
            return std::find(labels.begin(), labels.end(), label) != labels.end();
        }

    }  // namespace

    TEST_F(ValueExplainControlTest, AProgramAnswersItsValueAndTheTraceOfHowItWasComputed) {
        RegisterBattle();
        const Json response = Explain("doc", "damage", kBattle);
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        const Json& result = Result(response);
        EXPECT_EQ(StringOf(&result, "kind"), "Program");
        EXPECT_NE(StringOf(&result, "source").find("caster.attack"), String::npos);
        EXPECT_EQ(BoolOf(&result, "has_value"), true);
        EXPECT_EQ(StringOf(&result, "value"), "27");

        const Json* trace = FindJsonMember(result, "trace");
        ASSERT_NE(trace, nullptr);
        EXPECT_EQ(StringOf(trace, "source"), "Operation");
        EXPECT_EQ(StringOf(trace, "value"), "27");
        EXPECT_EQ(StringOf(trace, "type"), "Int32");
        Vector<String> labels;
        CollectLabels(*trace, labels);
        EXPECT_TRUE(Contains(labels, "caster.attack"));
        EXPECT_TRUE(Contains(labels, "cast.level"));
    }

    TEST_F(ValueExplainControlTest, ASlotAnswersOneNodeAndAConstHasNoTrace) {
        RegisterBattle();
        const Json slot = Result(Explain("doc", "aim", kBattle));
        EXPECT_EQ(StringOf(&slot, "kind"), "Slot");
        EXPECT_EQ(StringOf(&slot, "source"), "caster.velocity");
        EXPECT_EQ(StringOf(&slot, "value"), "(1, 2, 3)");
        const Json* trace = FindJsonMember(slot, "trace");
        ASSERT_NE(trace, nullptr);
        EXPECT_EQ(StringOf(trace, "source"), "ContextKey");
        EXPECT_EQ(StringOf(trace, "label"), "caster.velocity");
        EXPECT_EQ(CountNodes(*trace), 1U);

        const Json constant = Result(Explain("doc", "power", kBattle));
        EXPECT_EQ(StringOf(&constant, "kind"), "Const");
        EXPECT_EQ(StringOf(&constant, "value"), "1");
        EXPECT_EQ(FindJsonMember(constant, "trace"), nullptr);
    }

    TEST_F(ValueExplainControlTest, TheAnswerFollowsTheObjectsNow) {
        RegisterBattle();
        world.caster.attack = 100;
        EXPECT_EQ(StringOf(&Result(Explain("doc", "damage", kBattle)), "value"), "203");
    }

    TEST_F(ValueExplainControlTest, ARequestWithoutAContextUsesTheDefaultProviderAndWithoutOneAnswersOnlyKindAndSource) {
        const Json bare = Result(Explain("doc", "damage"));
        EXPECT_EQ(StringOf(&bare, "kind"), "Program");
        EXPECT_NE(StringOf(&bare, "source").find("caster.attack"), String::npos);
        EXPECT_EQ(BoolOf(&bare, "has_value"), false);
        EXPECT_NE(StringOf(&bare, "why").find("no context"), String::npos);
        EXPECT_EQ(FindJsonMember(bare, "value"), nullptr);
        EXPECT_EQ(FindJsonMember(bare, "trace"), nullptr);

        const Json constant = Result(Explain("doc", "power"));
        EXPECT_EQ(BoolOf(&constant, "has_value"), false);
        EXPECT_EQ(StringOf(&constant, "source"), "1");

        RegisterBattle(kDefaultContextName);
        const Json served = Result(Explain("doc", "damage"));
        EXPECT_EQ(BoolOf(&served, "has_value"), true);
        EXPECT_EQ(StringOf(&served, "value"), "27");
    }

    TEST_F(ValueExplainControlTest, AContextWithoutAnObjectAtALevelTheValueReadsGivesNoValueAndSaysWhich) {
        ValueContextRegistry::GetInstance().Register(kBattle, [this]() -> Optional<ExplainContext> {
            Context partial(world.declaration);
            partial.Set(0, world.caster);
            return ExplainContext{&world.declaration, partial};
        });
        const Json missing = Result(Explain("doc", "damage", kBattle));
        EXPECT_EQ(BoolOf(&missing, "has_value"), false);
        EXPECT_NE(StringOf(&missing, "why").find("cast"), String::npos);
        EXPECT_EQ(FindJsonMember(missing, "trace"), nullptr);
        // A value that reads only the level that is there still answers
        const Json present = Result(Explain("doc", "aim", kBattle));
        EXPECT_EQ(BoolOf(&present, "has_value"), true);
    }

    TEST_F(ValueExplainControlTest, AProviderWithNoContextNowAnswersLikeNoContext) {
        ValueContextRegistry::GetInstance().Register(kBattle, []() -> Optional<ExplainContext> { return std::nullopt; });
        const Json answer = Result(Explain("doc", "damage", kBattle));
        EXPECT_EQ(BoolOf(&answer, "has_value"), false);
        EXPECT_NE(StringOf(&answer, "why").find(kBattle.data()), String::npos);
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

        const Json result = Result(Explain("doc", "damage", kBattle));
        EXPECT_EQ(StringOf(&result, "value"), Format("{}", sum));
        EXPECT_EQ(BoolOf(&result, "truncated"), true);
        const Json* trace = FindJsonMember(result, "trace");
        ASSERT_NE(trace, nullptr);
        EXPECT_LE(CountNodes(*trace), kMaxFormattedLines);
        EXPECT_LE(DumpJson(result).size(), kMaxFormattedLines * 400U);
    }

    TEST_F(ValueExplainControlTest, TheRefusalsHaveTheirOwnCodes) {
        RegisterBattle();
        const Json noRoot = Explain("nothing", "damage", kBattle);
        EXPECT_FALSE(IsOk(noRoot));
        EXPECT_EQ(ErrorCode(noRoot), "unknown_root");
        EXPECT_EQ(ErrorCode(Explain("empty", "damage", kBattle)), "no_object");

        const Json notAValue = Explain("doc", "context", kBattle);
        EXPECT_EQ(ErrorCode(notAValue), "not_a_value");
        EXPECT_NE(ErrorMessage(notAValue).find("Value<"), String::npos);

        EXPECT_EQ(ErrorCode(Explain("doc", "damage", "elsewhere")), "unknown_context");
        const Json unknownContext = Explain("doc", "damage", "elsewhere");
        EXPECT_NE(ErrorMessage(unknownContext).find(kBattle.data()), String::npos);

        EXPECT_EQ(ErrorCode(Explain("behind", "spans[0].low", kBattle)), "not_fixed");
        EXPECT_EQ(ErrorCode(Explain("doc", "missing_field", kBattle)), "not_found");
    }

    TEST_F(ValueExplainControlTest, AMistypedRequestIsInvalidParams) {
        Json params = Json::object();
        params["root"] = "doc";
        params["path"] = 5;
        EXPECT_EQ(ErrorCode(CallControl(service, 1, "explain_value", params)), "invalid_params");
        EXPECT_EQ(ErrorCode(CallControl(service, 2, "explain_value", Json::object())), "invalid_params");
        params["path"] = "damage";
        params["context"] = 7;
        EXPECT_EQ(ErrorCode(CallControl(service, 3, "explain_value", params)), "invalid_params");
    }

    TEST_F(ValueExplainControlTest, AHostilePathIsRefusedAndNeverAbortsAndInternsNothing) {
        RegisterBattle();
        const String hostile = "rc_explain_hostile_path_7e21d4";
        const Vector<String> paths = {"a..b", String(kHostileLength, '['), String(kHostileLength, 'a'), "damage.", "[", "damage[", ".damage", "damage]]", "", hostile};
        for (const String& path : paths) {
            const Json response = Explain("doc", path, kBattle);
            EXPECT_FALSE(IsOk(response)) << path.substr(0, 20).c_str();
            EXPECT_FALSE(ErrorCode(response).empty());
        }
        EXPECT_FALSE(PoolString::Find(hostile).has_value());
        EXPECT_FALSE(PoolString::Find(kElsewhere).has_value());
        EXPECT_EQ(ErrorCode(Explain("doc", "damage", kElsewhere)), "unknown_context");
        EXPECT_FALSE(PoolString::Find(kElsewhere).has_value());
    }

    TEST_F(ValueExplainControlTest, TheMethodIsListedForClients) {
        const Json listed = Result(CallControl(service, 1, "list_methods"));
        const Json* methods = FindJsonMember(listed, "methods");
        ASSERT_NE(methods, nullptr);
        bool found = false;
        for (const Json& method : *methods) {
            found = found || StringOf(&method, "name") == "explain_value";
        }
        EXPECT_TRUE(found);
    }

}  // namespace BECore::Tests
