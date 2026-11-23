#include "SeatedAnswersTest.h"

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Control/SchemaExport.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <Common/ControlTestSupport.h>
#include <Common/ScratchScopeFixture.h>
#include <Generated/SeatedAnswersTest.gen.hpp>
#include <algorithm>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String WrittenActions(const SeatedActions& actions) {
            String text;
            JsonWriter writer(text);
            actions.WriteJson(writer);
            EXPECT_TRUE(writer.IsComplete());
            return text;
        }

        AnswerAction MakeAction(int32_t card) {
            AnswerAction action;
            action.card = card;
            action.type = "play";
            return action;
        }

        const TypeInfo& Require(const TypeRegistry& registry, StringView name) {
            const TypeInfo* info = registry.Find(name);
            EXPECT_NE(info, nullptr) << String(name).c_str();
            return info != nullptr ? *info : MakeTypeInfo<AnswerPoint>();
        }

        using SeatedAnswersSchemaTest = ScratchScopeFixture;

    }  // namespace

    TEST(SeatedAnswersTest, ADerivedViewWritesTheSortedKeysOfItsOwnFields) {
        AnswerView view;
        view.name = "table";
        AnswerPoint& point = view.points.emplace_back();
        point.x = 1;
        point.y = 2;
        view.turn = 3;

        const SeatedView& asBase = view;
        EXPECT_EQ(WrittenText(asBase), R"({"name":"table","points":[{"x":1,"y":2}],"turn":3})");
    }

    TEST(SeatedAnswersTest, ADerivedAnswerWritesItsOwnKeysThroughABasePointer) {
        UniquePtr<SeatedView> view = MakeUnique<AnswerView>();
        UniquePtr<SeatedOutcome> outcome = MakeUnique<AnswerOutcome>();
        EXPECT_EQ(WrittenText(*outcome), R"({"seat":0})");
        EXPECT_EQ(WrittenText(*view), R"({"name":"","points":[],"turn":0})");
    }

    TEST(SeatedAnswersTest, AnEmptyOutcomeBaseWritesAnEmptyObject) {
        const SeatedOutcome outcome;
        EXPECT_EQ(WrittenText(outcome), "{}");
    }

    TEST(SeatedAnswersTest, AnActionListWritesAnArrayWhateverItHolds) {
        SeatedActionList<AnswerAction> list;
        EXPECT_EQ(list.Count(), 0U);
        EXPECT_EQ(WrittenActions(list), "[]");

        list.Add(MakeAction(7));
        list.Add(MakeAction(2));
        EXPECT_EQ(list.Count(), 2U);
        ASSERT_EQ(list.Items().size(), 2U);
        EXPECT_EQ(list.Items()[1].card, 2);

        const SeatedActions& asBase = list;
        EXPECT_EQ(WrittenActions(asBase), R"([{"card":7,"type":"play"},{"card":2,"type":"play"}])");
    }

    TEST_F(SeatedAnswersSchemaTest, TheSchemaListsAGamesAnswersUnderTheBases) {
        // The registry of the program: the types register in static initializers, so this also proves the answer bases are linked into the executable
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        // Other test and game types may derive from the bases too, so each base is asked whether it lists this one
        const auto lists = [&registry](StringView base, StringView derived) {
            const Json described = ToJson(DescribeTypeSchema(Require(registry, base), registry));
            const Json* names = FindJsonMember(described, "derived");
            return names != nullptr && names->is_array() && std::find(names->begin(), names->end(), MakeJsonString(derived)) != names->end();
        };
        EXPECT_TRUE(lists("SeatedView", "AnswerView"));
        EXPECT_TRUE(lists("SeatedAction", "AnswerAction"));
        EXPECT_TRUE(lists("SeatedOutcome", "AnswerOutcome"));

        const Json view = ToJson(DescribeTypeSchema(Require(registry, "AnswerView"), registry));
        const Json* fields = FindJsonMember(view, "fields");
        ASSERT_TRUE(fields != nullptr && fields->is_array());
        ASSERT_EQ(fields->size(), 3U);
        EXPECT_EQ(GetJsonString((*fields)[0], "name"), String("name"));
        EXPECT_EQ(GetJsonString((*fields)[2], "name"), String("turn"));
    }

}  // namespace BECore::Tests
