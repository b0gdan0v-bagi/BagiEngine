// The by-name methods of the control API (Control/ReflectionControl.h): get_property and set_property over a root, and their codes.

#include "ReflectionControlTest.h"

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/Printers.h>
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

            static bool IsOk(const Json& response) {
                return GetJsonBool(response, "ok").value_or(false);
            }

            static String CodeOf(const Json& response) {
                const Json* error = FindJsonMember(response, "error");
                return error != nullptr ? GetJsonString(*error, "code").value_or(String()) : String();
            }

            static String MessageOf(const Json& response) {
                const Json* error = FindJsonMember(response, "error");
                return error != nullptr ? GetJsonString(*error, "message").value_or(String()) : String();
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
                EXPECT_EQ(CodeOf(response), String(code)) << String(path).c_str() << ": " << MessageOf(response).c_str();
                EXPECT_EQ(Snapshot(box), before) << String(path).c_str();
            }

            ControlService service;
            RcBox box;
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
            ASSERT_TRUE(IsOk(response)) << String(path).c_str() << ": " << MessageOf(response).c_str();
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
        EXPECT_EQ(GetJsonInt(*FindJsonMember(locked, "result"), "value").value_or(0), 7);
    }

    TEST_F(ReflectionControlTest, SetPropertyWritesEveryKindAndAnswersWithTheValueReadBack) {
        const auto write = [this](StringView path, Json value, const Json& expected) {
            const Json response = Set("box", path, std::move(value));
            ASSERT_TRUE(IsOk(response)) << String(path).c_str() << ": " << MessageOf(response).c_str();
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
        EXPECT_EQ(CodeOf(response), "not_a_value");
        ExpectRefused("box", "leaf", Json(1), "not_a_value");
    }

    TEST_F(ReflectionControlTest, ABadPathAndAMissingFieldAreRefused) {
        EXPECT_EQ(CodeOf(Get("box", "count..x")), "invalid_path");
        EXPECT_EQ(CodeOf(Get("box", "")), "invalid_path");
        EXPECT_EQ(CodeOf(Get("box", "nothing")), "not_found");
        EXPECT_EQ(CodeOf(Get("box", "numbers[0]")), "not_found");
        ExpectRefused("box", "nothing", Json(1), "not_found");
    }

    TEST_F(ReflectionControlTest, APathOverTheLimitsIsInvalid) {
        EXPECT_EQ(CodeOf(Get("box", String(1000, 'a'))), "invalid_path");
        String manySegments = "leaf";
        for (int i = 0; i < 40; ++i) {
            manySegments += ".id";
        }
        EXPECT_EQ(CodeOf(Get("box", manySegments)), "invalid_path");
        EXPECT_EQ(CodeOf(Set("box", String(1000, 'a'), Json(1))), "invalid_path");
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
                    EXPECT_FALSE(CodeOf(response).empty()) << String(path).c_str();
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
        EXPECT_EQ(CodeOf(Ask("get_property", noRoot)), "invalid_params");

        Json numberRoot = noRoot;
        numberRoot["root"] = 5;
        EXPECT_EQ(CodeOf(Ask("get_property", numberRoot)), "invalid_params");

        Json noPath = Json::object();
        noPath["root"] = MakeJsonString("box");
        EXPECT_EQ(CodeOf(Ask("get_property", noPath)), "invalid_params");

        Json arrayPath = noPath;
        arrayPath["path"] = Json::array({"count"});
        EXPECT_EQ(CodeOf(Ask("get_property", arrayPath)), "invalid_params");

        Json noValue = noPath;
        noValue["path"] = MakeJsonString("count");
        EXPECT_EQ(CodeOf(Ask("set_property", noValue)), "invalid_params");

        EXPECT_EQ(CodeOf(Ask("get_property", Json::object())), "invalid_params");
    }

    TEST_F(ReflectionControlTest, AnUnknownRootAndARootWithoutAnObjectAreRefused) {
        EXPECT_EQ(CodeOf(Get("nothing", "count")), "unknown_root");
        EXPECT_EQ(CodeOf(Set("nothing", "count", Json(1))), "unknown_root");
        EXPECT_EQ(CodeOf(Get("empty", "count")), "no_object");
        EXPECT_EQ(CodeOf(Set("empty", "count", Json(1))), "no_object");
    }

    TEST_F(ReflectionControlTest, TheRootIsResolvedOnEveryCall) {
        EXPECT_TRUE(IsOk(Get("box", "count")));
        service.Roots().Register("box", ControlRoot{[] { return TypedRef{}; }});
        EXPECT_EQ(CodeOf(Get("box", "count")), "no_object");
    }

    TEST_F(ReflectionControlTest, APoolStringFieldNeedsARootThatAllowsInterning) {
        ExpectRefused("box", "tag", MakeJsonString(kClientTag), "interning");
        EXPECT_FALSE(PoolString::Find(kClientTag).has_value());

        const Json response = Set("open", "tag", MakeJsonString("rc_tag_written_through_open"));
        ASSERT_TRUE(IsOk(response)) << MessageOf(response).c_str();
        EXPECT_EQ(box.tag.ToStringView(), "rc_tag_written_through_open");
    }

    TEST_F(ReflectionControlTest, TextOfAClientIsNeverInterned) {
        EXPECT_EQ(CodeOf(Get(kClientRoot, "count")), "unknown_root");
        EXPECT_EQ(CodeOf(Get("box", kClientPath)), "not_found");
        EXPECT_EQ(CodeOf(Set("box", kClientPath, MakeJsonString(kClientTag))), "not_found");
        EXPECT_EQ(CodeOf(Ask("get_property", Json::object())), "invalid_params");

        EXPECT_FALSE(PoolString::Find(kClientRoot).has_value());
        EXPECT_FALSE(PoolString::Find(kClientPath).has_value());
        EXPECT_FALSE(PoolString::Find(kClientTag).has_value());
    }

    TEST_F(ReflectionControlTest, ALongNameIsCutInTheMessage) {
        const String longRoot(5000, 'r');
        const String message = MessageOf(Get(longRoot, "count"));
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
        EXPECT_EQ(CodeOf(response), "invalid_params");
    }

}  // namespace BECore::Tests
