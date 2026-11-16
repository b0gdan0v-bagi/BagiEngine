// The reflected surface of StressGame: what the control API's by-name methods reach under the root `game`.

#include <BECore/Application/CommandLine.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <Common/EngineEnvironment.h>
#include <Stress/StressGame.h>
#include <gtest/gtest.h>

namespace BECore::Stress::Tests {

    namespace {

        class StressGameReflectionTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                BECore::Tests::EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                CoreManager::GetCommandLine().Set({});
                _game = New<StressGame>();
                _ref = TypedRef::Of(*_game);
                ASSERT_TRUE(_ref);
            }

            /// Calls @p method with the arguments of the JSON text and returns the JSON text of the result
            std::expected<String, CallError> Call(StringView method, StringView argumentsJson) {
                const auto parsed = ParseJson(argumentsJson);
                EXPECT_TRUE(parsed.has_value());
                const Json arguments = parsed ? *parsed : Json::object();
                JsonDeserializer reader(arguments);
                JsonSerializer writer;
                const auto outcome = CallMethod(_ref, method, reader, writer);
                if (!outcome) {
                    return std::unexpected(outcome.error());
                }
                return DumpJson(writer.Take());
            }

            IntrusivePtr<StressGame> _game;
            TypedRef _ref;
        };

    }  // namespace

    TEST_F(StressGameReflectionTest, TheWritableFieldReadsAndWritesAndKeepsItsRange) {
        EXPECT_EQ(GetProperty(_ref, "runNumber"), PropertyValue(uint64_t{0}));

        ASSERT_TRUE(SetProperty(_ref, "runNumber", PropertyValue(int64_t{42})).has_value());
        EXPECT_EQ(GetProperty(_ref, "runNumber"), PropertyValue(uint64_t{42}));

        const auto refused = SetProperty(_ref, "runNumber", PropertyValue(int64_t{1001}));
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, PropertyErrorCode::out_of_range);
        EXPECT_EQ(GetProperty(_ref, "runNumber"), PropertyValue(uint64_t{42}));
    }

    TEST_F(StressGameReflectionTest, TheVersionFieldIsReadOnlyAndFollowsTheState) {
        const auto refused = SetProperty(_ref, "stateVersion", PropertyValue(int64_t{7}));
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, PropertyErrorCode::read_only);

        _game->GetState().SetCount(11);  // what the control API's set_count does
        _game->SyncStateVersion();
        EXPECT_EQ(GetProperty(_ref, "stateVersion"), PropertyValue(_game->GetState().Version()));
    }

    TEST_F(StressGameReflectionTest, SetObjectCountChangesTheStateAndItsVersion) {
        const uint64_t version = _game->GetState().Version();
        const auto outcome = Call("SetObjectCount", R"({"count": 321})");
        ASSERT_TRUE(outcome.has_value());
        EXPECT_EQ(*outcome, "{}");
        EXPECT_EQ(_game->GetState().GetCount(), 321u);
        EXPECT_GT(_game->GetState().Version(), version);
        EXPECT_EQ(GetProperty(_ref, "stateVersion"), PropertyValue(_game->GetState().Version()));
    }

    TEST_F(StressGameReflectionTest, ACountAboveTheMaximumIsTheMaximum) {
        ASSERT_TRUE(Call("SetObjectCount", R"({"count": 4000000000})").has_value());
        EXPECT_EQ(_game->GetState().GetCount(), kMaxCount);
    }

    TEST_F(StressGameReflectionTest, ABadCountIsRefusedAndChangesNothing) {
        const uint32_t count = _game->GetState().GetCount();
        const uint64_t version = _game->GetState().Version();
        for (const char* arguments : {R"({"count": "many"})", R"({"count": -1})", "{}"}) {
            const auto outcome = Call("SetObjectCount", arguments);
            ASSERT_FALSE(outcome.has_value()) << arguments;
            EXPECT_EQ(outcome.error().status, CallStatus::bad_arguments) << arguments;
        }
        EXPECT_EQ(_game->GetState().GetCount(), count);
        EXPECT_EQ(_game->GetState().Version(), version);
    }

    TEST_F(StressGameReflectionTest, DescribeAnswersWhatTheStateShows) {
        _game->GetState().SetCount(250);
        _game->GetState().SetLanguage(StressLanguage::Ru);
        const auto outcome = Call("Describe", "{}");
        ASSERT_TRUE(outcome.has_value());
        EXPECT_EQ(*outcome, R"({"result":"scene=sprites count=250 language=ru"})");
    }

    TEST_F(StressGameReflectionTest, TheMethodsAreListedAsCallableWithTheirConstness) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* info = registry.Find("StressGame");
        ASSERT_NE(info, nullptr);

        const MethodRef setCount = registry.FindMethod(*info, "SetObjectCount");
        ASSERT_TRUE(setCount);
        EXPECT_TRUE(setCount.method->callable);
        EXPECT_FALSE(setCount.method->isConst);

        const MethodRef describe = registry.FindMethod(*info, "Describe");
        ASSERT_TRUE(describe);
        EXPECT_TRUE(describe.method->callable);
        EXPECT_TRUE(describe.method->isConst);
    }

}  // namespace BECore::Stress::Tests
