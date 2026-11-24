// The by-name methods of the control API (Control/ReflectionControl.h): get_property and set_property over a root, and their codes.

#include "ReflectionControlTest.h"

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/ReflectionControl.h>
#include <BECore/Control/SchemaExport.h>
#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/FreshText.h>
#include <Common/JsonDoc.h>
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

        /// The JSON text of a string value
        String Quoted(StringView text) {
            String quoted;
            JsonWriter writer(quoted);
            writer.Value(text);
            return quoted;
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

            /// Runs one request through the service and returns the answer; @p paramsText is the JSON text of the params
            ControlReply Ask(StringView method, StringView paramsText = "{}") {
                return CallControlText(service, ++nextId, method, paramsText);
            }

            static String CallParams(StringView root, StringView method, StringView argumentsText) {
                String text;
                JsonWriter writer(text);
                writer.BeginObject();
                writer.Member("root", root);
                writer.Member("method", method);
                writer.Key("arguments");
                writer.Raw(argumentsText);
                writer.EndObject();
                return text;
            }

            /// The params of `get_property` and, with a @p valueText, of `set_property`; the value is the text of a JSON value, written as the client wrote it
            static String PathParams(StringView root, StringView path, StringView valueText = {}) {
                String text;
                JsonWriter writer(text);
                writer.BeginObject();
                writer.Member("root", root);
                writer.Member("path", path);
                if (!valueText.empty()) {
                    writer.Key("value");
                    writer.Raw(valueText);
                }
                writer.EndObject();
                return text;
            }

            ControlReply Get(StringView root, StringView path) {
                return Ask("get_property", PathParams(root, path));
            }

            ControlReply Set(StringView root, StringView path, StringView valueText) {
                return Ask("set_property", PathParams(root, path, valueText));
            }

            ControlReply Call(StringView root, StringView method, StringView argumentsText) {
                return Ask("call_method", CallParams(root, method, argumentsText));
            }

            /// The text of the answer as it was sent
            String RawGet(StringView root, StringView path) {
                return String(Get(root, path).Text());
            }

            String RawSet(StringView root, StringView path, StringView valueText) {
                return String(Set(root, path, valueText).Text());
            }

            String RawCall(StringView root, StringView method, StringView argumentsText) {
                return String(Call(root, method, argumentsText).Text());
            }

            /// The `value` of an answer, not valid when there is none
            static JsonView ValueOf(const ControlReply& reply) {
                return MemberOf(reply.Result(), "value");
            }

            /// The `result` member of the answer's result, not valid when there is none
            static JsonView ResultOf(const ControlReply& reply) {
                return MemberOf(reply.Result(), "result");
            }

            /// A write that is refused with @p code and leaves the object as it was
            void ExpectRefused(StringView root, StringView path, StringView valueText, StringView code) {
                const String before = Snapshot(box);
                const ControlReply reply = Set(root, path, valueText);
                EXPECT_FALSE(reply.IsOk()) << String(path).c_str();
                EXPECT_EQ(reply.ErrorCode(), String(code)) << String(path).c_str() << ": " << reply.ErrorMessage().c_str();
                EXPECT_EQ(Snapshot(box), before) << String(path).c_str();
            }

            /// A call that is refused with @p code and did not run the method
            void ExpectCallRefused(StringView root, StringView method, StringView argumentsText, StringView code) {
                const int32_t before = tool.calls;
                const ControlReply reply = Call(root, method, argumentsText);
                EXPECT_FALSE(reply.IsOk()) << String(method).c_str();
                EXPECT_EQ(reply.ErrorCode(), String(code)) << String(method).c_str() << ": " << reply.ErrorMessage().c_str();
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

        const auto check = [this](StringView path, StringView expectedText) {
            const ControlReply reply = Get("box", path);
            ASSERT_TRUE(reply.IsOk()) << String(path).c_str() << ": " << reply.ErrorMessage().c_str();
            const JsonView value = ValueOf(reply);
            ASSERT_TRUE(value.IsValid());
            EXPECT_TRUE(JsonEquals(value, JsonDoc(expectedText))) << String(path).c_str() << ": " << String(value.Raw()).c_str();
        };
        check("flag", "true");
        check("count", "3");
        check("big", "-5000000000");
        check("huge", "9223372036854775808");
        check("ratio", "1.5");
        check("wide", "0.125");
        check("text", R"("text")");
        check("tag", R"("rc_tag_in_the_object")");
        check("mode", R"("On")");
        check("leaf.id", "42");
    }

    TEST_F(ReflectionControlTest, GetPropertyAnswersTheSameBytesForEveryKind) {
        box.flag = true;
        box.big = -5000000000;
        box.huge = 9223372036854775808ULL;
        box.ratio = 1.5f;
        box.wide = 0.1;
        box.text = "quote \" slash \\ newline \n caf\xC3\xA9 bad \xFF end";
        box.tag = "rc_tag_in_the_object"_intern;
        box.mode = RcMode::On;
        box.leaf.id = 42;

        Vector<String> replies;
        for (const StringView path : {"flag", "count", "big", "huge", "ratio", "wide", "text", "tag", "mode", "leaf.id"}) {
            replies.push_back(RawGet("box", path));
        }

        ExpectTexts(replies,
                    {R"json({"id":1,"ok":true,"result":{"path":"flag","root":"box","value":true}})json", R"json({"id":2,"ok":true,"result":{"path":"count","root":"box","value":3}})json",
                     R"json({"id":3,"ok":true,"result":{"path":"big","root":"box","value":-5000000000}})json",
                     R"json({"id":4,"ok":true,"result":{"path":"huge","root":"box","value":9223372036854775808}})json",
                     R"json({"id":5,"ok":true,"result":{"path":"ratio","root":"box","value":1.5}})json", R"json({"id":6,"ok":true,"result":{"path":"wide","root":"box","value":0.1}})json",
                     R"json({"id":7,"ok":true,"result":{"path":"text","root":"box","value":"quote \" slash \\ newline \n café bad � end"}})json",
                     R"json({"id":8,"ok":true,"result":{"path":"tag","root":"box","value":"rc_tag_in_the_object"}})json",
                     R"json({"id":9,"ok":true,"result":{"path":"mode","root":"box","value":"On"}})json", R"json({"id":10,"ok":true,"result":{"path":"leaf.id","root":"box","value":42}})json"});
    }

    TEST_F(ReflectionControlTest, SetPropertyAnswersTheSameBytesWithTheValueReadBack) {
        Vector<String> replies;
        replies.push_back(RawSet("box", "count", "7"));
        replies.push_back(RawSet("box", "text", R"("a \"quoted\" café\n")"));
        replies.push_back(RawSet("box", "flag", "true"));
        replies.push_back(RawSet("box", "wide", "0.1"));
        replies.push_back(RawSet("box", "big", "-5000000000"));
        replies.push_back(RawSet("box", "huge", "9223372036854775808"));
        replies.push_back(RawSet("box", "mode", R"("On")"));
        replies.push_back(RawSet("box", "ratio", "2.0"));

        ExpectTexts(replies, {R"json({"id":1,"ok":true,"result":{"path":"count","root":"box","value":7}})json",
                              R"json({"id":2,"ok":true,"result":{"path":"text","root":"box","value":"a \"quoted\" café\n"}})json",
                              R"json({"id":3,"ok":true,"result":{"path":"flag","root":"box","value":true}})json", R"json({"id":4,"ok":true,"result":{"path":"wide","root":"box","value":0.1}})json",
                              R"json({"id":5,"ok":true,"result":{"path":"big","root":"box","value":-5000000000}})json",
                              R"json({"id":6,"ok":true,"result":{"path":"huge","root":"box","value":9223372036854775808}})json",
                              R"json({"id":7,"ok":true,"result":{"path":"mode","root":"box","value":"On"}})json", R"json({"id":8,"ok":true,"result":{"path":"ratio","root":"box","value":2.0}})json"});
    }

    TEST_F(ReflectionControlTest, CallMethodAnswersTheSameBytesWithAndWithoutAResult) {
        Vector<String> replies;
        replies.push_back(RawCall("tool", "Add", R"({"first":2,"second":3})"));
        replies.push_back(RawCall("tool", "Echo", R"({"text":"line \"one\"\ncafé"})"));
        replies.push_back(RawCall("tool", "Shift", R"({"leaf":{"id":4,"label":"x"}})"));
        replies.push_back(RawCall("tool", "Total", R"({"numbers":[1,2,3]})"));
        replies.push_back(RawCall("tool", "Touch", "{}"));
        replies.push_back(RawCall("tool", "Count", "{}"));

        ExpectTexts(replies, {R"json({"id":1,"ok":true,"result":{"method":"Add","result":5,"root":"tool"}})json",
                              R"json({"id":2,"ok":true,"result":{"method":"Echo","result":"line \"one\"\ncafé","root":"tool"}})json",
                              R"json({"id":3,"ok":true,"result":{"method":"Shift","result":{"id":5,"label":"x"},"root":"tool"}})json",
                              R"json({"id":4,"ok":true,"result":{"method":"Total","result":6,"root":"tool"}})json", R"json({"id":5,"ok":true,"result":{"method":"Touch","root":"tool"}})json",
                              R"json({"id":6,"ok":true,"result":{"method":"Count","result":5,"root":"tool"}})json"});
    }

    TEST_F(ReflectionControlTest, ASecondGetPropertyOfANumberAllocatesNothingBeyondWhatItReads) {
        // The path walk of GetProperty allocates whoever asks: a method that reads the same field and answers `{}` is the measure of the rest
        service.RegisterHandler("reads_only", [this](ControlCall& call) {
            const auto value = GetProperty(TypedRef::Of(box), "count");
            EXPECT_TRUE(value.has_value());
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.EndObject();
            });
        });
        const String params = PathParams("box", "count");
        // Post() parses the request and Drain() makes the call object, and both allocate whatever the handler is, so the two Drain() calls are compared after a first round
        // that grew the reply buffer
        const auto drained = [&](StringView method) {
            service.Post(MakeRequestText(++nextId, method, params), [](const String&) {});
            return CountAllocations([this] { service.Drain(); });
        };
        drained("get_property");
        drained("reads_only");

        const AllocCount get = drained("get_property");
        const AllocCount reads = drained("reads_only");

        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_EQ(get.count, reads.count);
        EXPECT_EQ(get.bytes, reads.bytes);
    }

    TEST_F(ReflectionControlTest, TheAnswerEchoesTheRootAndThePath) {
        const ControlReply reply = Get("box", "leaf.id");
        ASSERT_TRUE(reply.IsOk());
        EXPECT_EQ(StringOf(reply.Result(), "root"), "box");
        EXPECT_EQ(StringOf(reply.Result(), "path"), "leaf.id");
    }

    TEST_F(ReflectionControlTest, ASecretFieldCanBeReadAndALockedOneToo) {
        EXPECT_TRUE(Get("box", "secret").IsOk());
        const ControlReply locked = Get("box", "locked");
        ASSERT_TRUE(locked.IsOk());
        EXPECT_EQ(IntOf(locked.Result(), "value").value_or(0), 7);
    }

    TEST_F(ReflectionControlTest, SetPropertyWritesEveryKindAndAnswersWithTheValueReadBack) {
        const auto write = [this](StringView path, StringView valueText, StringView expectedText) {
            const ControlReply reply = Set("box", path, valueText);
            ASSERT_TRUE(reply.IsOk()) << String(path).c_str() << ": " << reply.ErrorMessage().c_str();
            const JsonView readBack = ValueOf(reply);
            ASSERT_TRUE(readBack.IsValid());
            EXPECT_TRUE(JsonEquals(readBack, JsonDoc(expectedText))) << String(path).c_str() << ": " << String(readBack.Raw()).c_str();
        };
        write("flag", "true", "true");
        write("count", "9", "9");
        write("big", "-7", "-7");
        write("huge", "18446744073709551615", "18446744073709551615");
        write("ratio", "0.75", "0.75");
        write("wide", "2", "2.0");
        write("text", R"("changed")", R"("changed")");
        write("mode", R"("Off")", R"("Off")");
        write("mode", "2", R"("On")");
        write("leaf.label", R"("deep")", R"("deep")");

        EXPECT_TRUE(box.flag);
        EXPECT_EQ(box.big, -7);
        EXPECT_EQ(box.huge, 18446744073709551615ULL);
        EXPECT_EQ(box.text, "changed");
        EXPECT_EQ(box.mode, RcMode::On);
        EXPECT_EQ(box.leaf.label, "deep");
    }

    TEST_F(ReflectionControlTest, ASecretFieldRefusesAWrite) {
        ExpectRefused("box", "secret", "1", "hidden");
    }

    TEST_F(ReflectionControlTest, ALockedFieldRefusesAWrite) {
        ExpectRefused("box", "locked", "1", "read_only");
    }

    TEST_F(ReflectionControlTest, AWrongKindOfValueIsATypeMismatch) {
        ExpectRefused("box", "count", R"("5")", "type_mismatch");
        ExpectRefused("box", "count", "5.0", "type_mismatch");
        ExpectRefused("box", "flag", "1", "type_mismatch");
        ExpectRefused("box", "text", "1", "type_mismatch");
    }

    TEST_F(ReflectionControlTest, ANumberOutsideTheTypeOrTheRangeIsOutOfRange) {
        ExpectRefused("box", "count", "11", "out_of_range");
        ExpectRefused("box", "count", "-1", "out_of_range");
        ExpectRefused("box", "count", "5000000000", "out_of_range");
        ExpectRefused("box", "huge", "-1", "out_of_range");
        ExpectRefused("box", "mode", "3", "out_of_range");
    }

    TEST_F(ReflectionControlTest, AStructIsNotAValue) {
        EXPECT_EQ(Get("box", "leaf").ErrorCode(), "not_a_value");
        ExpectRefused("box", "leaf", "1", "not_a_value");
    }

    TEST_F(ReflectionControlTest, ABadPathAndAMissingFieldAreRefused) {
        EXPECT_EQ(Get("box", "count..x").ErrorCode(), "invalid_path");
        EXPECT_EQ(Get("box", "").ErrorCode(), "invalid_path");
        EXPECT_EQ(Get("box", "nothing").ErrorCode(), "not_found");
        EXPECT_EQ(Get("box", "numbers[0]").ErrorCode(), "not_found");
        ExpectRefused("box", "nothing", "1", "not_found");
    }

    TEST_F(ReflectionControlTest, APathOverTheLimitsIsInvalid) {
        EXPECT_EQ(Get("box", String(1000, 'a')).ErrorCode(), "invalid_path");
        String manySegments = "leaf";
        for (int i = 0; i < 40; ++i) {
            manySegments += ".id";
        }
        EXPECT_EQ(Get("box", manySegments).ErrorCode(), "invalid_path");
        EXPECT_EQ(Set("box", String(1000, 'a'), "1").ErrorCode(), "invalid_path");
    }

    TEST_F(ReflectionControlTest, AValueThatIsNoBoolNumberOrStringIsInvalidParams) {
        ExpectRefused("box", "count", "null", "invalid_params");
        ExpectRefused("box", "count", "[1,2]", "invalid_params");
        ExpectRefused("box", "count", "{}", "invalid_params");
        ExpectRefused("box", "count", "[[[1]]]", "invalid_params");
    }

    TEST_F(ReflectionControlTest, ANumberThatIsNotFiniteOrTooBigNeverWritesAnInteger) {
        for (const StringView text : {"1e999", "-1e999", "1e308", "123456789012345678901234567890"}) {
            for (const StringView path : {"count", "big", "huge", "ratio", "wide"}) {
                const ControlReply reply = Set("box", path, text);
                if (reply.IsOk()) {
                    // Only a float field takes a double, and the value read back is a finite number
                    EXPECT_TRUE(path == "ratio" || path == "wide") << String(path).c_str() << " " << String(text).c_str();
                } else {
                    EXPECT_FALSE(reply.ErrorCode().empty()) << String(path).c_str();
                }
            }
            EXPECT_EQ(box._count, 3);
            EXPECT_EQ(box.big, 0);
            EXPECT_EQ(box.huge, 0U);
        }
    }

    TEST_F(ReflectionControlTest, ABigIntegerValueIsReadByItsKind) {
        EXPECT_TRUE(Set("box", "huge", "18446744073709551615").IsOk());
        EXPECT_EQ(box.huge, std::numeric_limits<uint64_t>::max());
        EXPECT_TRUE(Set("box", "big", "-9223372036854775808").IsOk());
        EXPECT_EQ(box.big, std::numeric_limits<int64_t>::min());
        // Past 64 bits it is a number, which a double field takes and an integer field refuses
        EXPECT_TRUE(Set("box", "wide", "123456789012345678901234567890").IsOk());
        EXPECT_EQ(box.wide, 1.2345678901234568e29);
        EXPECT_FALSE(Set("box", "huge", "123456789012345678901234567890").IsOk());
        EXPECT_EQ(box.huge, std::numeric_limits<uint64_t>::max());
    }

    TEST_F(ReflectionControlTest, ANumberBeyondADoubleIsInvalidParamsNotAParseError) {
        const ControlReply reply = Set("box", "wide", "1e999");

        EXPECT_EQ(reply.ErrorCode(), "invalid_params");
        EXPECT_EQ(reply.ErrorMessage(), "'value' has to be a finite number");
        EXPECT_EQ(box.wide, 0.25);
    }

    TEST_F(ReflectionControlTest, AMissingOrWrongTypedParamIsInvalidParams) {
        EXPECT_EQ(Ask("get_property", R"({"path":"count"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("get_property", R"({"path":"count","root":5})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("get_property", R"({"root":"box"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("get_property", R"({"root":"box","path":["count"]})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("set_property", R"({"root":"box","path":"count"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("get_property").ErrorCode(), "invalid_params");
    }

    TEST_F(ReflectionControlTest, AnUnknownRootAndARootWithoutAnObjectAreRefused) {
        EXPECT_EQ(Get("nothing", "count").ErrorCode(), "unknown_root");
        EXPECT_EQ(Set("nothing", "count", "1").ErrorCode(), "unknown_root");
        EXPECT_EQ(Get("empty", "count").ErrorCode(), "no_object");
        EXPECT_EQ(Set("empty", "count", "1").ErrorCode(), "no_object");
    }

    TEST_F(ReflectionControlTest, TheRootIsResolvedOnEveryCall) {
        EXPECT_TRUE(Get("box", "count").IsOk());
        service.Roots().Register("box", ControlRoot{[] { return TypedRef{}; }});
        EXPECT_EQ(Get("box", "count").ErrorCode(), "no_object");
    }

    TEST_F(ReflectionControlTest, APoolStringFieldNeedsARootThatAllowsInterning) {
        ExpectRefused("box", "tag", Quoted(kClientTag), "interning");
        EXPECT_FALSE(PoolString::Find(kClientTag).has_value());

        const ControlReply reply = Set("open", "tag", R"("rc_tag_written_through_open")");
        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        EXPECT_EQ(box.tag.ToStringView(), "rc_tag_written_through_open");
    }

    TEST_F(ReflectionControlTest, TextOfAClientIsNeverInterned) {
        EXPECT_EQ(Get(kClientRoot, "count").ErrorCode(), "unknown_root");
        EXPECT_EQ(Get("box", kClientPath).ErrorCode(), "not_found");
        EXPECT_EQ(Set("box", kClientPath, Quoted(kClientTag)).ErrorCode(), "not_found");
        EXPECT_EQ(Ask("get_property").ErrorCode(), "invalid_params");

        EXPECT_FALSE(PoolString::Find(kClientRoot).has_value());
        EXPECT_FALSE(PoolString::Find(kClientPath).has_value());
        EXPECT_FALSE(PoolString::Find(kClientTag).has_value());
    }

    TEST_F(ReflectionControlTest, ALongNameIsCutInTheMessage) {
        const String longRoot(5000, 'r');
        const String message = Get(longRoot, "count").ErrorMessage();
        EXPECT_LT(message.size(), 200U);
    }

    TEST_F(ReflectionControlTest, TheMethodsAreRegisteredByTheBuiltins) {
        ControlService other;
        RegisterControlBuiltins(other);
        EXPECT_EQ(CallControlText(other, 1, "get_property").ErrorCode(), "invalid_params");
    }

    // =========================================================================
    // call_method
    // =========================================================================

    TEST_F(ReflectionControlTest, CallMethodRunsTheMethodAndAnswersWithItsResult) {
        const ControlReply reply = Call("tool", "Add", R"({"first":2,"second":3})");
        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        EXPECT_EQ(StringOf(reply.Result(), "root"), "tool");
        EXPECT_EQ(StringOf(reply.Result(), "method"), "Add");
        EXPECT_EQ(RawOf(reply.Result(), "result"), "5");
        EXPECT_EQ(tool.calls, 1);
    }

    TEST_F(ReflectionControlTest, CallMethodTakesTextListsAndObjects) {
        EXPECT_EQ(RawOf(Call("tool", "Echo", R"({"text":"hello"})").Result(), "result"), R"("hello")");

        EXPECT_EQ(RawOf(Call("tool", "Total", R"({"numbers":[1,2,3]})").Result(), "result"), "6");

        const ControlReply shifted = Call("tool", "Shift", R"({"leaf":{"id":4,"label":"x"}})");
        ASSERT_TRUE(shifted.IsOk()) << shifted.ErrorMessage().c_str();
        ASSERT_TRUE(ResultOf(shifted).IsValid());
        EXPECT_EQ(IntOf(ResultOf(shifted), "id").value_or(0), 5);
        EXPECT_EQ(StringOf(ResultOf(shifted), "label"), "x");
    }

    TEST_F(ReflectionControlTest, TheMembersOfAnObjectResultComeInDeclarationOrder) {
        const String reply = RawCall("tool", "Ordered", "{}");

        EXPECT_EQ(reply, R"json({"id":1,"ok":true,"result":{"method":"Ordered","result":{"zeta":1,"alpha":2,"middle":{"id":0,"label":""}},"root":"tool"}})json");
    }

    TEST_F(ReflectionControlTest, AnArrayResultIsAnArrayOfObjects) {
        const String reply = RawCall("tool", "Leaves", "{}");

        EXPECT_EQ(reply, R"json({"id":1,"ok":true,"result":{"method":"Leaves","result":[{"id":1,"label":"a"},{"id":2,"label":"b"}],"root":"tool"}})json");
    }

    TEST_F(ReflectionControlTest, AMethodThatReturnsNothingHasNoResultAndNeedsNoArguments) {
        const ControlReply reply = Ask("call_method", R"({"root":"tool","method":"Touch"})");
        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        EXPECT_FALSE(ResultOf(reply).IsValid());
        EXPECT_EQ(tool.calls, 1);
    }

    TEST_F(ReflectionControlTest, AConstMethodRunsOnAConstRoot) {
        tool.calls = 4;
        const ControlReply reply = Call("tool_const", "Count", "{}");
        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        EXPECT_EQ(RawOf(reply.Result(), "result"), "4");
    }

    TEST_F(ReflectionControlTest, AMethodThatChangesTheObjectIsRefusedOnAConstRoot) {
        ExpectCallRefused("tool_const", "Add", R"({"first":1,"second":1})", "const_object");
    }

    TEST_F(ReflectionControlTest, BadArgumentsAreRefusedAndTheMethodDoesNotRun) {
        ExpectCallRefused("tool", "Add", "{}", "bad_arguments");
        ExpectCallRefused("tool", "Add", R"({"first":1})", "bad_arguments");
        ExpectCallRefused("tool", "Add", R"({"first":"1","second":2})", "bad_arguments");
        ExpectCallRefused("tool", "Add", R"({"first":1,"second":2,"third":3})", "bad_arguments");
        ExpectCallRefused("tool", "Add", R"({"first":5000000000,"second":2})", "bad_arguments");
    }

    TEST_F(ReflectionControlTest, ANameThatIsNoMethodOrNoCallableMethodIsRefused) {
        ExpectCallRefused("tool", "Nothing", "{}", "unknown_function");
        ExpectCallRefused("tool", "Self", "{}", "not_callable");
    }

    TEST_F(ReflectionControlTest, ARootThatIsUnknownOrHasNoObjectIsRefused) {
        EXPECT_EQ(Call("nothing", "Touch", "{}").ErrorCode(), "unknown_root");
        EXPECT_EQ(Call("empty", "Touch", "{}").ErrorCode(), "no_object");
    }

    TEST_F(ReflectionControlTest, AMethodWithTextForThePoolNeedsARootThatAllowsInterning) {
        // The open root interns the text at the end, so it is a text of its own: kClientTag stays out of the pool for the other cases
        const String clientTag = FreshText("rc_method_tag");
        const String name = Format(R"({{"name":{}}})", Quoted(StringView(clientTag)));
        ExpectCallRefused("tool", "Name", name, "interning");
        EXPECT_FALSE(PoolString::Find(StringView(clientTag)).has_value());

        const String tagged = Format(R"({{"tagged":{{"id":1,"tag":{}}}}})", Quoted(StringView(clientTag)));
        ExpectCallRefused("tool", "Tag", tagged, "interning");
        ExpectCallRefused("tool", "Tag", "{}", "interning");  // refused before the arguments are read
        EXPECT_FALSE(PoolString::Find(StringView(clientTag)).has_value());

        EXPECT_TRUE(Call("tool_open", "Name", name).IsOk());
        EXPECT_TRUE(Call("tool_open", "Tag", tagged).IsOk());
        EXPECT_TRUE(PoolString::Find(StringView(clientTag)).has_value());
        EXPECT_EQ(tool.calls, 2);
    }

    TEST_F(ReflectionControlTest, ArgumentsThatAreNoObjectAreInvalidParams) {
        for (const StringView arguments : {"[1]", "\"text\"", "5", "null"}) {
            ExpectCallRefused("tool", "Touch", arguments, "invalid_params");
        }
    }

    TEST_F(ReflectionControlTest, ARequestNestedOverTheServiceLimitIsInvalid) {
        const size_t levels = ControlService::kMaxRequestDepth + 9;
        String deep(levels, '[');
        deep.append(levels, ']');
        const String arguments = Format(R"({{"numbers":{}}})", deep);
        EXPECT_EQ(Call("tool", "Total", arguments).ErrorCode(), "invalid_request");
        EXPECT_EQ(tool.calls, 0);
    }

    TEST_F(ReflectionControlTest, ADeepArgumentOfARecursiveTypeIsReadOrRefusedNeverCrashes) {
        constexpr size_t kLevels = 50;
        String node = R"({"id":1,"children":[]})";
        for (size_t level = 0; level < kLevels; ++level) {
            node = Format(R"({{"id":1,"children":[{}]}})", node);
        }
        const ControlReply reply = Call("tool", "Nest", Format(R"({{"node":{}}})", node));
        EXPECT_TRUE(reply.IsOk() || reply.ErrorCode() == "bad_arguments") << reply.ErrorMessage().c_str();
    }

    TEST_F(ReflectionControlTest, ALongTextArgumentReachesTheMethodWhole) {
        const String longText(100000, 'x');
        const ControlReply reply = Call("tool", "Echo", Format(R"({{"text":{}}})", Quoted(StringView(longText))));
        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        EXPECT_EQ(StringOf(reply.Result(), "result").size(), longText.size());
    }

    TEST_F(ReflectionControlTest, ALongTextInTheWrongPlaceIsCutInTheMessage) {
        const String longText(100000, 'y');
        const ControlReply reply = Call("tool", "Add", Format(R"({{"first":{},"second":1}})", Quoted(StringView(longText))));
        EXPECT_EQ(reply.ErrorCode(), "bad_arguments");
        EXPECT_LT(reply.ErrorMessage().size(), 2000U);
        EXPECT_EQ(tool.calls, 0);
    }

    TEST_F(ReflectionControlTest, ANameOfAMethodTheClientSendsIsNeverInterned) {
        EXPECT_EQ(Call("tool", kClientPath, "{}").ErrorCode(), "unknown_function");
        EXPECT_FALSE(PoolString::Find(kClientPath).has_value());
    }

    TEST_F(ReflectionControlTest, ParamsOfCallMethodAreChecked) {
        EXPECT_EQ(Ask("call_method", R"({"root":"tool"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("call_method", R"({"root":"tool","method":3})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("call_method", R"({"method":"Touch"})").ErrorCode(), "invalid_params");
    }

    // =========================================================================
    // list_types, describe_type, schema
    // =========================================================================

    namespace {

        constexpr StringView kClientType = "rc_client_type_name_7c3a90";

        String Named(StringView key, StringView value) {
            String text;
            JsonWriter writer(text);
            writer.BeginObject();
            writer.Member(key, value);
            writer.EndObject();
            return text;
        }

        JsonView ResultMember(const ControlReply& reply, StringView key) {
            return MemberOf(reply.Result(), key);
        }

        Vector<String> TypeNames(const ControlReply& reply) {
            Vector<String> names;
            const JsonView types = ResultMember(reply, "types");
            if (!types.IsArray()) {
                ADD_FAILURE() << "no types array";
                return names;
            }
            for (const JsonView entry : types.Items()) {
                names.push_back(StringOf(entry, "name"));
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
        const ControlReply reply = Ask("list_types");
        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        const Vector<String> names = TypeNames(reply);
        EXPECT_TRUE(Contains(names, "RcBox"));
        EXPECT_TRUE(Contains(names, "RcTool"));
        for (size_t i = 1; i < names.size(); ++i) {
            EXPECT_LT(StringView(names[i - 1]), StringView(names[i]));
        }
    }

    TEST_F(ReflectionControlTest, ListTypesWithABaseListsItsDerivedTypes) {
        // The registry's own hierarchy: IWidget is a factory base with derived widgets
        const ControlReply reply = Ask("list_types", Named("base", "IWidget"));
        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        const Vector<String> names = TypeNames(reply);
        EXPECT_FALSE(names.empty());
        EXPECT_FALSE(Contains(names, "IWidget"));
        EXPECT_FALSE(Contains(names, "RcBox"));

        const Vector<String> directNames = TypeNames(Ask("list_types", R"({"base":"IWidget","recursive":false})"));
        EXPECT_FALSE(directNames.empty());
        EXPECT_LE(directNames.size(), names.size());
    }

    TEST_F(ReflectionControlTest, ListTypesCreatableOnlyDropsAbstractAndUnregisteredTypes) {
        const Vector<String> creatable = TypeNames(Ask("list_types", R"({"creatable":true})"));
        const Vector<String> all = TypeNames(Ask("list_types"));
        EXPECT_LT(creatable.size(), all.size());
        EXPECT_FALSE(Contains(creatable, "RcBox"));
    }

    TEST_F(ReflectionControlTest, ListTypesRefusesAnUnknownBaseAndBadParams) {
        EXPECT_EQ(Ask("list_types", Named("base", kClientType)).ErrorCode(), "unknown_type");
        EXPECT_EQ(Ask("list_types", R"({"base":3})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("list_types", R"({"base":"IWidget","recursive":"yes"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("list_types", R"({"creatable":1})").ErrorCode(), "invalid_params");
    }

    TEST_F(ReflectionControlTest, DescribeTypeAnswersTheTypeWithItsFieldsAndMethods) {
        const ControlReply reply = Ask("describe_type", Named("name", "RcTool"));
        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        const JsonView result = reply.Result();
        ASSERT_TRUE(result.IsValid());
        EXPECT_EQ(StringOf(result, "name"), "RcTool");
        const JsonView methods = MemberOf(result, "methods");
        ASSERT_TRUE(methods.IsArray());
        EXPECT_GT(methods.Size(), 0U);
        EXPECT_TRUE(MemberOf(result, "allFields").IsValid());

        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* info = registry.Find("RcTool");
        ASSERT_NE(info, nullptr);
        EXPECT_EQ(String(result.Raw()), WrittenText(DescribeTypeSchema(*info, registry)));
    }

    TEST_F(ReflectionControlTest, DescribeTypeRefusesAnUnknownTypeAndBadParams) {
        EXPECT_EQ(Ask("describe_type", Named("name", kClientType)).ErrorCode(), "unknown_type");
        EXPECT_EQ(Ask("describe_type").ErrorCode(), "invalid_params");
        EXPECT_EQ(Ask("describe_type", R"({"name":5})").ErrorCode(), "invalid_params");
    }

    TEST_F(ReflectionControlTest, ATypeNameFromAClientIsNeverInterned) {
        EXPECT_FALSE(PoolString::Find(kClientType).has_value());
        (void)Ask("describe_type", Named("name", kClientType));
        (void)Ask("list_types", Named("base", kClientType));
        EXPECT_FALSE(PoolString::Find(kClientType).has_value());
    }

    TEST_F(ReflectionControlTest, SchemaEqualsTheExportOfTheRegistryAndListsTheRoots) {
        const ControlReply reply = Ask("schema");
        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        const JsonView result = reply.Result();
        ASSERT_TRUE(result.IsValid());
        EXPECT_EQ(String(result.Raw()), WrittenText(MakeSchemaAnswer(TypeRegistry::GetInstance(), service.Roots())));

        const JsonView roots = MemberOf(result, "roots");
        ASSERT_TRUE(roots.IsArray());
        EXPECT_EQ(roots.Size(), service.Roots().Names().size());
        bool sawBox = false;
        bool sawEmpty = false;
        for (const JsonView root : roots.Items()) {
            const String name = StringOf(root, "name");
            if (name == "box") {
                sawBox = true;
                EXPECT_EQ(StringOf(root, "type"), "RcBox");
            }
            if (name == "empty") {
                sawEmpty = true;
                EXPECT_FALSE(MemberOf(root, "type").IsValid());
            }
        }
        EXPECT_TRUE(sawBox);
        EXPECT_TRUE(sawEmpty);
    }

    TEST_F(ReflectionControlTest, EveryMethodAnswersNoObjectForTheGameRootWithoutAnActiveGame) {
        RegisterGameControl(service);
        EXPECT_EQ(Get("game", "x").ErrorCode(), "no_object");
        EXPECT_EQ(Set("game", "x", "1").ErrorCode(), "no_object");
        EXPECT_EQ(Ask("call_method", R"({"root":"game","method":"Anything"})").ErrorCode(), "no_object");

        const ControlReply schema = Ask("schema");
        const JsonView roots = ResultMember(schema, "roots");
        ASSERT_TRUE(roots.IsArray());
        bool sawGame = false;
        for (const JsonView root : roots.Items()) {
            if (StringOf(root, "name") == "game") {
                sawGame = true;
                EXPECT_FALSE(MemberOf(root, "type").IsValid());
            }
        }
        EXPECT_TRUE(sawGame);
        EXPECT_TRUE(Ask("list_types").IsOk());
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
