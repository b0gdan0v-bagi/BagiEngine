// The by-name methods of the control API (Control/ReflectionControl.h): get_property and set_property over a root, and their codes.

#include "ReflectionControlTest.h"

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/ReflectionControl.h>
#include <BECore/Control/SchemaExport.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/ControlTestSupport.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Games/GameControl.h>
#include <Generated/ReflectionControlTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ReflectionControlData;

    namespace {

        constexpr StringView kClientTag = "rc_client_tag_value_3b7e1f";
        constexpr StringView kClientRoot = "rc_client_root_name_9d41c2";
        constexpr StringView kClientPath = "rc_client_path_name_5a08e6";

        /// The saved text of an object: two equal states give equal text
        String Snapshot(RcBox& box) {
            const TypedRef ref = TypedRef::Of(box);
            XmlSerializer serializer;
            serializer.BeginObject("object");
            ref.type->serialize(ref.object, serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        class ReflectionControlTest : public ::testing::Test {
        protected:
            void SetUp() override {
                RegisterControlBuiltins(service);
                service.Roots().Register("box", ControlRoot{[this] { return TypedRef::Of(box); }});
                service.Roots().Register("open", ControlRoot{[this] { return TypedRef::Of(box); }, true});
                service.Roots().Register("tool", ControlRoot{[this] { return TypedRef::Of(tool); }});
                service.Roots().Register("tool_open", ControlRoot{[this] { return TypedRef::Of(tool); }, true});
                service.Roots().Register("tool_const", ControlRoot{[this] { return TypedRef::Of(std::as_const(tool)); }});
                service.Roots().Register("empty", ControlRoot{[] { return TypedRef{}; }});
            }

            /// Runs one request through the service and returns the answer
            Json Ask(StringView method, Json params) {
                Json request = Json::object();
                request["id"] = ++nextId;
                request["method"] = MakeJsonString(method);
                request["params"] = std::move(params);
                Json response;
                service.Post(DumpJson(request), [&response](String text) {
                    auto parsed = ParseJson(text);
                    response = parsed ? std::move(*parsed) : Json();
                });
                service.Drain();
                return response;
            }

            Json Get(StringView root, StringView path) {
                Json params = Json::object();
                params["root"] = MakeJsonString(root);
                params["path"] = MakeJsonString(path);
                return Ask("get_property", std::move(params));
            }

            Json Set(StringView root, StringView path, Json value) {
                Json params = Json::object();
                params["root"] = MakeJsonString(root);
                params["path"] = MakeJsonString(path);
                params["value"] = std::move(value);
                return Ask("set_property", std::move(params));
            }

            /// The `value` of an answer, null when there is none
            static const Json* ValueOf(const Json& response) {
                const Json* result = FindJsonMember(response, "result");
                return result != nullptr ? FindJsonMember(*result, "value") : nullptr;
            }

            /// A write that is refused with @p code and leaves the object as it was
            void ExpectRefused(StringView root, StringView path, Json value, StringView code) {
                const String before = Snapshot(box);
                const Json response = Set(root, path, std::move(value));
                EXPECT_FALSE(IsOk(response)) << String(path).c_str();
                EXPECT_EQ(ErrorCode(response), String(code)) << String(path).c_str() << ": " << ErrorMessage(response).c_str();
                EXPECT_EQ(Snapshot(box), before) << String(path).c_str();
            }

            Json Call(StringView root, StringView method, Json arguments) {
                Json params = Json::object();
                params["root"] = MakeJsonString(root);
                params["method"] = MakeJsonString(method);
                params["arguments"] = std::move(arguments);
                return Ask("call_method", std::move(params));
            }

            /// The `result` member of the answer's result, null when there is none
            static const Json* ResultOf(const Json& response) {
                const Json* result = FindJsonMember(response, "result");
                return result != nullptr ? FindJsonMember(*result, "result") : nullptr;
            }

            /// A call that is refused with @p code and did not run the method
            void ExpectCallRefused(StringView root, StringView method, Json arguments, StringView code) {
                const int32_t before = tool.calls;
                const Json response = Call(root, method, std::move(arguments));
                EXPECT_FALSE(IsOk(response)) << String(method).c_str();
                EXPECT_EQ(ErrorCode(response), String(code)) << String(method).c_str() << ": " << ErrorMessage(response).c_str();
                EXPECT_EQ(tool.calls, before) << String(method).c_str();
            }

            ControlService service;
            RcBox box;
            RcTool tool;
            int64_t nextId = 0;
        };

    }  // namespace

    TEST_F(ReflectionControlTest, GetPropertyReadsEveryKind) {
        box.flag = true;
        box.big = -5000000000;
        box.huge = 9223372036854775808ULL;
        box.ratio = 1.5f;
        box.wide = 0.125;
        box.tag = "rc_tag_in_the_object"_intern;
        box.mode = RcMode::On;
        box.leaf.id = 42;

        const auto check = [this](StringView path, const Json& expected) {
            const Json response = Get("box", path);
            ASSERT_TRUE(IsOk(response)) << String(path).c_str() << ": " << ErrorMessage(response).c_str();
            const Json* value = ValueOf(response);
            ASSERT_NE(value, nullptr);
            EXPECT_EQ(*value, expected) << String(path).c_str();
        };
        check("flag", Json(true));
        check("count", Json(int64_t{3}));
        check("big", Json(int64_t{-5000000000}));
        check("huge", Json(uint64_t{9223372036854775808ULL}));
        check("ratio", Json(1.5));
        check("wide", Json(0.125));
        check("text", MakeJsonString("text"));
        check("tag", MakeJsonString("rc_tag_in_the_object"));
        check("mode", MakeJsonString("On"));
        check("leaf.id", Json(int64_t{42}));
    }

    TEST_F(ReflectionControlTest, TheAnswerEchoesTheRootAndThePath) {
        const Json response = Get("box", "leaf.id");
        ASSERT_TRUE(IsOk(response));
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonString(*result, "root").value_or(String()), "box");
        EXPECT_EQ(GetJsonString(*result, "path").value_or(String()), "leaf.id");
    }

    TEST_F(ReflectionControlTest, ASecretFieldCanBeReadAndALockedOneToo) {
        EXPECT_TRUE(IsOk(Get("box", "secret")));
        const Json locked = Get("box", "locked");
        ASSERT_TRUE(IsOk(locked));
        EXPECT_EQ(GetJsonInt(Require(FindJsonMember(locked, "result")), "value").value_or(0), 7);
    }

    TEST_F(ReflectionControlTest, SetPropertyWritesEveryKindAndAnswersWithTheValueReadBack) {
        const auto write = [this](StringView path, Json value, const Json& expected) {
            const Json response = Set("box", path, std::move(value));
            ASSERT_TRUE(IsOk(response)) << String(path).c_str() << ": " << ErrorMessage(response).c_str();
            const Json* readBack = ValueOf(response);
            ASSERT_NE(readBack, nullptr);
            EXPECT_EQ(*readBack, expected) << String(path).c_str();
        };
        write("flag", Json(true), Json(true));
        write("count", Json(9), Json(9));
        write("big", Json(int64_t{-7}), Json(int64_t{-7}));
        write("huge", Json(uint64_t{18446744073709551615ULL}), Json(uint64_t{18446744073709551615ULL}));
        write("ratio", Json(0.75), Json(0.75));
        write("wide", Json(2), Json(2.0));
        write("text", MakeJsonString("changed"), MakeJsonString("changed"));
        write("mode", MakeJsonString("Off"), MakeJsonString("Off"));
        write("mode", Json(2), MakeJsonString("On"));
        write("leaf.label", MakeJsonString("deep"), MakeJsonString("deep"));

        EXPECT_TRUE(box.flag);
        EXPECT_EQ(box.big, -7);
        EXPECT_EQ(box.huge, 18446744073709551615ULL);
        EXPECT_EQ(box.text, "changed");
        EXPECT_EQ(box.mode, RcMode::On);
        EXPECT_EQ(box.leaf.label, "deep");
    }

    TEST_F(ReflectionControlTest, ASecretFieldRefusesAWrite) {
        ExpectRefused("box", "secret", Json(1), "hidden");
    }

    TEST_F(ReflectionControlTest, ALockedFieldRefusesAWrite) {
        ExpectRefused("box", "locked", Json(1), "read_only");
    }

    TEST_F(ReflectionControlTest, AWrongKindOfValueIsATypeMismatch) {
        ExpectRefused("box", "count", MakeJsonString("5"), "type_mismatch");
        ExpectRefused("box", "count", Json(5.0), "type_mismatch");
        ExpectRefused("box", "flag", Json(1), "type_mismatch");
        ExpectRefused("box", "text", Json(1), "type_mismatch");
    }

    TEST_F(ReflectionControlTest, ANumberOutsideTheTypeOrTheRangeIsOutOfRange) {
        ExpectRefused("box", "count", Json(11), "out_of_range");
        ExpectRefused("box", "count", Json(-1), "out_of_range");
        ExpectRefused("box", "count", Json(int64_t{5000000000}), "out_of_range");
        ExpectRefused("box", "huge", Json(-1), "out_of_range");
        ExpectRefused("box", "mode", Json(3), "out_of_range");
    }

    TEST_F(ReflectionControlTest, AStructIsNotAValue) {
        const Json response = Get("box", "leaf");
        EXPECT_EQ(ErrorCode(response), "not_a_value");
        ExpectRefused("box", "leaf", Json(1), "not_a_value");
    }

    TEST_F(ReflectionControlTest, ABadPathAndAMissingFieldAreRefused) {
        EXPECT_EQ(ErrorCode(Get("box", "count..x")), "invalid_path");
        EXPECT_EQ(ErrorCode(Get("box", "")), "invalid_path");
        EXPECT_EQ(ErrorCode(Get("box", "nothing")), "not_found");
        EXPECT_EQ(ErrorCode(Get("box", "numbers[0]")), "not_found");
        ExpectRefused("box", "nothing", Json(1), "not_found");
    }

    TEST_F(ReflectionControlTest, APathOverTheLimitsIsInvalid) {
        EXPECT_EQ(ErrorCode(Get("box", String(1000, 'a'))), "invalid_path");
        String manySegments = "leaf";
        for (int i = 0; i < 40; ++i) {
            manySegments += ".id";
        }
        EXPECT_EQ(ErrorCode(Get("box", manySegments)), "invalid_path");
        EXPECT_EQ(ErrorCode(Set("box", String(1000, 'a'), Json(1))), "invalid_path");
    }

    TEST_F(ReflectionControlTest, AValueThatIsNoBoolNumberOrStringIsInvalidParams) {
        ExpectRefused("box", "count", Json(), "invalid_params");
        ExpectRefused("box", "count", Json::array({1, 2}), "invalid_params");
        ExpectRefused("box", "count", Json::object(), "invalid_params");
        ExpectRefused("box", "count", Json::array({Json::array({Json::array({1})})}), "invalid_params");
    }

    TEST_F(ReflectionControlTest, ANumberThatIsNotFiniteOrTooBigNeverWritesAnInteger) {
        for (const StringView text : {"1e999", "-1e999", "1e308", "123456789012345678901234567890"}) {
            const auto parsed = ParseJson(text);
            if (!parsed.has_value()) {
                continue;  // a parser may refuse the text itself; then there is nothing to send
            }
            for (const StringView path : {"count", "big", "huge", "ratio", "wide"}) {
                const Json response = Set("box", path, *parsed);
                if (IsOk(response)) {
                    // Only a float field takes a double, and the value read back is a finite number
                    EXPECT_TRUE(path == "ratio" || path == "wide") << String(path).c_str() << " " << String(text).c_str();
                } else {
                    EXPECT_FALSE(ErrorCode(response).empty()) << String(path).c_str();
                }
            }
            EXPECT_EQ(box._count, 3);
            EXPECT_EQ(box.big, 0);
            EXPECT_EQ(box.huge, 0U);
        }
    }

    TEST_F(ReflectionControlTest, AMissingOrWrongTypedParamIsInvalidParams) {
        Json noRoot = Json::object();
        noRoot["path"] = MakeJsonString("count");
        EXPECT_EQ(ErrorCode(Ask("get_property", noRoot)), "invalid_params");

        Json numberRoot = noRoot;
        numberRoot["root"] = 5;
        EXPECT_EQ(ErrorCode(Ask("get_property", numberRoot)), "invalid_params");

        Json noPath = Json::object();
        noPath["root"] = MakeJsonString("box");
        EXPECT_EQ(ErrorCode(Ask("get_property", noPath)), "invalid_params");

        Json arrayPath = noPath;
        arrayPath["path"] = Json::array({"count"});
        EXPECT_EQ(ErrorCode(Ask("get_property", arrayPath)), "invalid_params");

        Json noValue = noPath;
        noValue["path"] = MakeJsonString("count");
        EXPECT_EQ(ErrorCode(Ask("set_property", noValue)), "invalid_params");

        EXPECT_EQ(ErrorCode(Ask("get_property", Json::object())), "invalid_params");
    }

    TEST_F(ReflectionControlTest, AnUnknownRootAndARootWithoutAnObjectAreRefused) {
        EXPECT_EQ(ErrorCode(Get("nothing", "count")), "unknown_root");
        EXPECT_EQ(ErrorCode(Set("nothing", "count", Json(1))), "unknown_root");
        EXPECT_EQ(ErrorCode(Get("empty", "count")), "no_object");
        EXPECT_EQ(ErrorCode(Set("empty", "count", Json(1))), "no_object");
    }

    TEST_F(ReflectionControlTest, TheRootIsResolvedOnEveryCall) {
        EXPECT_TRUE(IsOk(Get("box", "count")));
        service.Roots().Register("box", ControlRoot{[] { return TypedRef{}; }});
        EXPECT_EQ(ErrorCode(Get("box", "count")), "no_object");
    }

    TEST_F(ReflectionControlTest, APoolStringFieldNeedsARootThatAllowsInterning) {
        ExpectRefused("box", "tag", MakeJsonString(kClientTag), "interning");
        EXPECT_FALSE(PoolString::Find(kClientTag).has_value());

        const Json response = Set("open", "tag", MakeJsonString("rc_tag_written_through_open"));
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        EXPECT_EQ(box.tag.ToStringView(), "rc_tag_written_through_open");
    }

    TEST_F(ReflectionControlTest, TextOfAClientIsNeverInterned) {
        EXPECT_EQ(ErrorCode(Get(kClientRoot, "count")), "unknown_root");
        EXPECT_EQ(ErrorCode(Get("box", kClientPath)), "not_found");
        EXPECT_EQ(ErrorCode(Set("box", kClientPath, MakeJsonString(kClientTag))), "not_found");
        EXPECT_EQ(ErrorCode(Ask("get_property", Json::object())), "invalid_params");

        EXPECT_FALSE(PoolString::Find(kClientRoot).has_value());
        EXPECT_FALSE(PoolString::Find(kClientPath).has_value());
        EXPECT_FALSE(PoolString::Find(kClientTag).has_value());
    }

    TEST_F(ReflectionControlTest, ALongNameIsCutInTheMessage) {
        const String longRoot(5000, 'r');
        const String message = ErrorMessage(Get(longRoot, "count"));
        EXPECT_LT(message.size(), 200U);
    }

    TEST_F(ReflectionControlTest, TheMethodsAreRegisteredByTheBuiltins) {
        ControlService other;
        RegisterControlBuiltins(other);
        Json response;
        Json request = Json::object();
        request["id"] = 1;
        request["method"] = MakeJsonString("get_property");
        other.Post(DumpJson(request), [&response](String text) {
            auto parsed = ParseJson(text);
            response = parsed ? std::move(*parsed) : Json();
        });
        other.Drain();
        EXPECT_EQ(ErrorCode(response), "invalid_params");
    }

    // =========================================================================
    // call_method
    // =========================================================================

    TEST_F(ReflectionControlTest, CallMethodRunsTheMethodAndAnswersWithItsResult) {
        Json arguments = Json::object();
        arguments["first"] = 2;
        arguments["second"] = 3;
        const Json response = Call("tool", "Add", arguments);
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonString(*result, "root").value_or(String()), "tool");
        EXPECT_EQ(GetJsonString(*result, "method").value_or(String()), "Add");
        ASSERT_NE(ResultOf(response), nullptr);
        EXPECT_EQ(*ResultOf(response), Json(5));
        EXPECT_EQ(tool.calls, 1);
    }

    TEST_F(ReflectionControlTest, CallMethodTakesTextListsAndObjects) {
        Json echo = Json::object();
        echo["text"] = MakeJsonString("hello");
        EXPECT_EQ(*ResultOf(Call("tool", "Echo", echo)), MakeJsonString("hello"));

        Json total = Json::object();
        total["numbers"] = Json::array({1, 2, 3});
        EXPECT_EQ(*ResultOf(Call("tool", "Total", total)), Json(6));

        Json leaf = Json::object();
        leaf["id"] = 4;
        leaf["label"] = MakeJsonString("x");
        Json shift = Json::object();
        shift["leaf"] = leaf;
        const Json shifted = Call("tool", "Shift", shift);
        ASSERT_TRUE(IsOk(shifted)) << ErrorMessage(shifted).c_str();
        ASSERT_NE(ResultOf(shifted), nullptr);
        EXPECT_EQ(GetJsonInt(*ResultOf(shifted), "id").value_or(0), 5);
        EXPECT_EQ(GetJsonString(*ResultOf(shifted), "label").value_or(String()), "x");
    }

    TEST_F(ReflectionControlTest, AMethodThatReturnsNothingHasNoResultAndNeedsNoArguments) {
        Json params = Json::object();
        params["root"] = MakeJsonString("tool");
        params["method"] = MakeJsonString("Touch");
        const Json response = Ask("call_method", params);
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        EXPECT_EQ(ResultOf(response), nullptr);
        EXPECT_EQ(tool.calls, 1);
    }

    TEST_F(ReflectionControlTest, AConstMethodRunsOnAConstRoot) {
        tool.calls = 4;
        const Json response = Call("tool_const", "Count", Json::object());
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        EXPECT_EQ(*ResultOf(response), Json(4));
    }

    TEST_F(ReflectionControlTest, AMethodThatChangesTheObjectIsRefusedOnAConstRoot) {
        Json arguments = Json::object();
        arguments["first"] = 1;
        arguments["second"] = 1;
        ExpectCallRefused("tool_const", "Add", arguments, "const_object");
    }

    TEST_F(ReflectionControlTest, BadArgumentsAreRefusedAndTheMethodDoesNotRun) {
        ExpectCallRefused("tool", "Add", Json::object(), "bad_arguments");

        Json missing = Json::object();
        missing["first"] = 1;
        ExpectCallRefused("tool", "Add", missing, "bad_arguments");

        Json wrongType = Json::object();
        wrongType["first"] = MakeJsonString("1");
        wrongType["second"] = 2;
        ExpectCallRefused("tool", "Add", wrongType, "bad_arguments");

        Json unknownKey = Json::object();
        unknownKey["first"] = 1;
        unknownKey["second"] = 2;
        unknownKey["third"] = 3;
        ExpectCallRefused("tool", "Add", unknownKey, "bad_arguments");

        Json outOfRange = Json::object();
        outOfRange["first"] = int64_t{5000000000};
        outOfRange["second"] = 2;
        ExpectCallRefused("tool", "Add", outOfRange, "bad_arguments");
    }

    TEST_F(ReflectionControlTest, ANameThatIsNoMethodOrNoCallableMethodIsRefused) {
        ExpectCallRefused("tool", "Nothing", Json::object(), "unknown_function");
        ExpectCallRefused("tool", "Self", Json::object(), "not_callable");
    }

    TEST_F(ReflectionControlTest, ARootThatIsUnknownOrHasNoObjectIsRefused) {
        EXPECT_EQ(ErrorCode(Call("nothing", "Touch", Json::object())), "unknown_root");
        EXPECT_EQ(ErrorCode(Call("empty", "Touch", Json::object())), "no_object");
    }

    TEST_F(ReflectionControlTest, AMethodWithTextForThePoolNeedsARootThatAllowsInterning) {
        // The open root interns the text at the end, so it is a text of its own: kClientTag stays out of the pool for the other cases
        const String clientTag = FreshText("rc_method_tag");
        Json name = Json::object();
        name["name"] = MakeJsonString(clientTag);
        ExpectCallRefused("tool", "Name", name, "interning");
        EXPECT_FALSE(PoolString::Find(StringView(clientTag)).has_value());

        Json tagged = Json::object();
        tagged["tagged"] = Json{{"id", 1}, {"tag", MakeJsonString(clientTag)}};
        ExpectCallRefused("tool", "Tag", tagged, "interning");
        ExpectCallRefused("tool", "Tag", Json::object(), "interning");  // refused before the arguments are read
        EXPECT_FALSE(PoolString::Find(StringView(clientTag)).has_value());

        EXPECT_TRUE(IsOk(Call("tool_open", "Name", name)));
        EXPECT_TRUE(IsOk(Call("tool_open", "Tag", tagged)));
        EXPECT_TRUE(PoolString::Find(StringView(clientTag)).has_value());
        EXPECT_EQ(tool.calls, 2);
    }

    TEST_F(ReflectionControlTest, ArgumentsThatAreNoObjectAreInvalidParams) {
        for (Json arguments : {Json::array({1}), MakeJsonString("text"), Json(5), Json()}) {
            ExpectCallRefused("tool", "Touch", arguments, "invalid_params");
        }
    }

    TEST_F(ReflectionControlTest, ARequestNestedOverTheServiceLimitIsInvalid) {
        Json deep = Json::array();
        for (size_t level = 0; level < ControlService::kMaxRequestDepth + 8; ++level) {
            deep = Json::array({std::move(deep)});
        }
        Json arguments = Json::object();
        arguments["numbers"] = std::move(deep);
        Json params = Json::object();
        params["root"] = MakeJsonString("tool");
        params["method"] = MakeJsonString("Total");
        params["arguments"] = std::move(arguments);
        EXPECT_EQ(ErrorCode(Ask("call_method", params)), "invalid_request");
        EXPECT_EQ(tool.calls, 0);
    }

    TEST_F(ReflectionControlTest, ADeepArgumentOfARecursiveTypeIsReadOrRefusedNeverCrashes) {
        constexpr size_t kLevels = 50;
        Json node = Json{{"id", 1}, {"children", Json::array()}};
        for (size_t level = 0; level < kLevels; ++level) {
            node = Json{{"id", 1}, {"children", Json::array({std::move(node)})}};
        }
        Json arguments = Json::object();
        arguments["node"] = std::move(node);
        const Json response = Call("tool", "Nest", arguments);
        EXPECT_TRUE(IsOk(response) || ErrorCode(response) == "bad_arguments") << ErrorMessage(response).c_str();
    }

    TEST_F(ReflectionControlTest, ALongTextArgumentReachesTheMethodWhole) {
        const String longText(100000, 'x');
        Json arguments = Json::object();
        arguments["text"] = MakeJsonString(longText);
        const Json response = Call("tool", "Echo", arguments);
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(response, "result")), "result").value_or(String()).size(), longText.size());
    }

    TEST_F(ReflectionControlTest, ALongTextInTheWrongPlaceIsCutInTheMessage) {
        Json arguments = Json::object();
        arguments["first"] = MakeJsonString(String(100000, 'y'));
        arguments["second"] = 1;
        const Json response = Call("tool", "Add", arguments);
        EXPECT_EQ(ErrorCode(response), "bad_arguments");
        EXPECT_LT(ErrorMessage(response).size(), 2000U);
        EXPECT_EQ(tool.calls, 0);
    }

    TEST_F(ReflectionControlTest, ANameOfAMethodTheClientSendsIsNeverInterned) {
        EXPECT_EQ(ErrorCode(Call("tool", kClientPath, Json::object())), "unknown_function");
        EXPECT_FALSE(PoolString::Find(kClientPath).has_value());
    }

    TEST_F(ReflectionControlTest, ParamsOfCallMethodAreChecked) {
        Json noMethod = Json::object();
        noMethod["root"] = MakeJsonString("tool");
        EXPECT_EQ(ErrorCode(Ask("call_method", noMethod)), "invalid_params");

        Json numberMethod = noMethod;
        numberMethod["method"] = 3;
        EXPECT_EQ(ErrorCode(Ask("call_method", numberMethod)), "invalid_params");

        Json noRoot = Json::object();
        noRoot["method"] = MakeJsonString("Touch");
        EXPECT_EQ(ErrorCode(Ask("call_method", noRoot)), "invalid_params");
    }

    // =========================================================================
    // list_types, describe_type, schema
    // =========================================================================

    namespace {

        constexpr StringView kClientType = "rc_client_type_name_7c3a90";

        Json Named(const char* key, StringView value) {
            Json params = Json::object();
            params[key] = MakeJsonString(value);
            return params;
        }

        const Json* ResultMember(const Json& response, StringView key) {
            const Json* result = FindJsonMember(response, "result");
            return result != nullptr ? FindJsonMember(*result, key) : nullptr;
        }

        Vector<String> TypeNames(const Json& response) {
            Vector<String> names;
            const Json* types = ResultMember(response, "types");
            if (types == nullptr || !types->is_array()) {
                ADD_FAILURE() << "no types array";
                return names;
            }
            for (const Json& entry : *types) {
                names.push_back(GetJsonString(entry, "name").value_or(String()));
            }
            return names;
        }

        bool Contains(const Vector<String>& names, StringView name) {
            for (const String& entry : names) {
                if (StringView(entry) == name) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TEST_F(ReflectionControlTest, ListTypesListsEveryTypeSortedByName) {
        const Json response = Ask("list_types", Json::object());
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        const Vector<String> names = TypeNames(response);
        EXPECT_TRUE(Contains(names, "RcBox"));
        EXPECT_TRUE(Contains(names, "RcTool"));
        for (size_t i = 1; i < names.size(); ++i) {
            EXPECT_LT(StringView(names[i - 1]), StringView(names[i]));
        }
    }

    TEST_F(ReflectionControlTest, ListTypesWithABaseListsItsDerivedTypes) {
        // The registry's own hierarchy: IWidget is a factory base with derived widgets
        const Json response = Ask("list_types", Named("base", "IWidget"));
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        const Vector<String> names = TypeNames(response);
        EXPECT_FALSE(names.empty());
        EXPECT_FALSE(Contains(names, "IWidget"));
        EXPECT_FALSE(Contains(names, "RcBox"));

        Json direct = Named("base", "IWidget");
        direct["recursive"] = false;
        const Vector<String> directNames = TypeNames(Ask("list_types", direct));
        EXPECT_FALSE(directNames.empty());
        EXPECT_LE(directNames.size(), names.size());
    }

    TEST_F(ReflectionControlTest, ListTypesCreatableOnlyDropsAbstractAndUnregisteredTypes) {
        Json params = Json::object();
        params["creatable"] = true;
        const Vector<String> creatable = TypeNames(Ask("list_types", params));
        const Vector<String> all = TypeNames(Ask("list_types", Json::object()));
        EXPECT_LT(creatable.size(), all.size());
        EXPECT_FALSE(Contains(creatable, "RcBox"));
    }

    TEST_F(ReflectionControlTest, ListTypesRefusesAnUnknownBaseAndBadParams) {
        EXPECT_EQ(ErrorCode(Ask("list_types", Named("base", kClientType))), "unknown_type");
        Json numberBase = Json::object();
        numberBase["base"] = 3;
        EXPECT_EQ(ErrorCode(Ask("list_types", numberBase)), "invalid_params");

        Json badRecursive = Named("base", "IWidget");
        badRecursive["recursive"] = "yes";
        EXPECT_EQ(ErrorCode(Ask("list_types", badRecursive)), "invalid_params");

        Json badCreatable = Json::object();
        badCreatable["creatable"] = 1;
        EXPECT_EQ(ErrorCode(Ask("list_types", badCreatable)), "invalid_params");
    }

    TEST_F(ReflectionControlTest, DescribeTypeAnswersTheTypeWithItsFieldsAndMethods) {
        const Json response = Ask("describe_type", Named("name", "RcTool"));
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonString(*result, "name").value_or(String()), "RcTool");
        const Json* methods = FindJsonMember(*result, "methods");
        ASSERT_TRUE(methods != nullptr && methods->is_array());
        EXPECT_FALSE(methods->empty());
        EXPECT_NE(FindJsonMember(*result, "allFields"), nullptr);

        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* info = registry.Find("RcTool");
        ASSERT_NE(info, nullptr);
        EXPECT_EQ(*result, DescribeTypeJson(*info, registry));
    }

    TEST_F(ReflectionControlTest, DescribeTypeRefusesAnUnknownTypeAndBadParams) {
        EXPECT_EQ(ErrorCode(Ask("describe_type", Named("name", kClientType))), "unknown_type");
        EXPECT_EQ(ErrorCode(Ask("describe_type", Json::object())), "invalid_params");
        Json numberName = Json::object();
        numberName["name"] = 5;
        EXPECT_EQ(ErrorCode(Ask("describe_type", numberName)), "invalid_params");
    }

    TEST_F(ReflectionControlTest, ATypeNameFromAClientIsNeverInterned) {
        EXPECT_FALSE(PoolString::Find(kClientType).has_value());
        (void)Ask("describe_type", Named("name", kClientType));
        (void)Ask("list_types", Named("base", kClientType));
        EXPECT_FALSE(PoolString::Find(kClientType).has_value());
    }

    TEST_F(ReflectionControlTest, SchemaEqualsTheExportOfTheRegistryAndListsTheRoots) {
        const Json response = Ask("schema", Json::object());
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response).c_str();
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(*result, BuildSchemaJson(TypeRegistry::GetInstance(), service.Roots()));

        const Json* roots = FindJsonMember(*result, "roots");
        ASSERT_TRUE(roots != nullptr && roots->is_array());
        EXPECT_EQ(roots->size(), service.Roots().Names().size());
        bool sawBox = false;
        bool sawEmpty = false;
        for (const Json& root : *roots) {
            const String name = GetJsonString(root, "name").value_or(String());
            if (name == "box") {
                sawBox = true;
                EXPECT_EQ(GetJsonString(root, "type").value_or(String()), "RcBox");
            }
            if (name == "empty") {
                sawEmpty = true;
                EXPECT_EQ(FindJsonMember(root, "type"), nullptr);
            }
        }
        EXPECT_TRUE(sawBox);
        EXPECT_TRUE(sawEmpty);
    }

    TEST_F(ReflectionControlTest, EveryMethodAnswersNoObjectForTheGameRootWithoutAnActiveGame) {
        RegisterGameControl(service);
        Json named = Json::object();
        named["root"] = "game";
        named["path"] = "x";
        EXPECT_EQ(ErrorCode(Ask("get_property", named)), "no_object");
        named["value"] = 1;
        EXPECT_EQ(ErrorCode(Ask("set_property", named)), "no_object");
        Json call = Json::object();
        call["root"] = "game";
        call["method"] = "Anything";
        EXPECT_EQ(ErrorCode(Ask("call_method", call)), "no_object");

        const Json schema = Ask("schema", Json::object());
        const Json* roots = ResultMember(schema, "roots");
        ASSERT_TRUE(roots != nullptr && roots->is_array());
        bool sawGame = false;
        for (const Json& root : *roots) {
            if (GetJsonString(root, "name").value_or(String()) == "game") {
                sawGame = true;
                EXPECT_EQ(FindJsonMember(root, "type"), nullptr);
            }
        }
        EXPECT_TRUE(sawGame);
        EXPECT_TRUE(IsOk(Ask("list_types", Json::object())));
    }

    // =========================================================================
    // MethodInterns
    // =========================================================================

    namespace {

        const MethodDesc& MethodOf(StringView type, StringView name) {
            static const MethodDesc kMissing;
            const TypeInfo* info = TypeRegistry::GetInstance().Find(type);
            EXPECT_NE(info, nullptr) << String(type).c_str();
            if (info == nullptr) {
                return kMissing;
            }
            const MethodRef found = TypeRegistry::GetInstance().FindMethod(*info, name);
            EXPECT_TRUE(found) << String(name).c_str();
            return found ? *found.method : kMissing;
        }

        bool Interns(StringView name) {
            return MethodInterns(MethodOf("RcTool", name));
        }

    }  // namespace

    TEST(ReflectionControlMethodInternsTest, APoolStringParameterInterns) {
        EXPECT_TRUE(Interns("Name"));
    }

    TEST(ReflectionControlMethodInternsTest, AnObjectParameterWithAPoolStringFieldInterns) {
        EXPECT_TRUE(Interns("Tag"));
    }

    TEST(ReflectionControlMethodInternsTest, AVariantAlternativeAndAMapValueAreLookedInto) {
        EXPECT_TRUE(Interns("Choose"));
        EXPECT_TRUE(Interns("Table"));
    }

    TEST(ReflectionControlMethodInternsTest, PlainParametersDoNotIntern) {
        for (const StringView name : {"Add", "Echo", "Total", "Shift", "Touch", "Count", "Self"}) {
            EXPECT_FALSE(Interns(name)) << String(name).c_str();
        }
    }

    TEST(ReflectionControlMethodInternsTest, ARecursiveTypeEndsTheWalkWhetherOrNotItHoldsAPoolString) {
        EXPECT_FALSE(Interns("Nest"));
        EXPECT_TRUE(Interns("NestNamed"));
    }

}  // namespace BECore::Tests
