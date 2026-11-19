// CallMethod and MethodDesc::invoke (Reflection/MethodCall.h): every status, the arguments by kind, the parent chain, the virtual call.

#include "MethodCallTest.h"

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestText.h>
#include <Generated/MethodCallTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::MethodCallData {

    bool McCounter::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    void McCounter::Fill(Vector<int32_t>& out) {
        out.push_back(total);
    }

    McCounter* McCounter::Self() {
        return this;
    }

    void McCounter::Adopt(IntrusivePtrNonAtomic<McBase> /*object*/) {}

    Vector<IntrusivePtrNonAtomic<McBase>> McCounter::Held() const {
        return {};
    }

}  // namespace BECore::MethodCallData

namespace BECore::Tests {

    using namespace MethodCallData;

    namespace {

        template <typename T>
        TypedRef Ref(T& object) {
            const TypedRef ref = TypedRef::Of(object);
            EXPECT_TRUE(ref);
            return ref;
        }

        struct Outcome {
            bool ran = false;
            CallStatus status = CallStatus::unknown_method;
            String message;
            String result;
        };

        /// Calls @p method with the arguments of the JSON text; the result is the JSON text of what the method wrote
        Outcome Call(TypedRef ref, StringView method, StringView argumentsJson) {
            auto parsed = ParseJson(argumentsJson);
            EXPECT_TRUE(parsed.has_value()) << String(argumentsJson).c_str();
            const Json arguments = parsed ? std::move(*parsed) : Json::object();
            JsonDeserializer reader(arguments);
            JsonSerializer writer;
            const auto outcome = CallMethod(ref, method, reader, writer);
            Outcome made;
            made.ran = outcome.has_value();
            if (!outcome.has_value()) {
                made.status = outcome.error().status;
                made.message = outcome.error().message;
            }
            made.result = DumpJson(writer.Take());
            return made;
        }

        void ExpectRefused(TypedRef ref, StringView method, StringView argumentsJson, CallStatus status, StringView messagePart) {
            const Outcome outcome = Call(ref, method, argumentsJson);
            EXPECT_FALSE(outcome.ran) << String(method).c_str();
            EXPECT_EQ(outcome.status, status) << outcome.message.c_str();
            EXPECT_TRUE(Contains(outcome.message, messagePart)) << outcome.message.c_str() << " lacks " << String(messagePart).c_str();
            EXPECT_EQ(outcome.result, "{}") << "a refused call writes no result";
        }

        void ExpectRan(TypedRef ref, StringView method, StringView argumentsJson, StringView resultJson) {
            const Outcome outcome = Call(ref, method, argumentsJson);
            EXPECT_TRUE(outcome.ran) << String(method).c_str() << ": " << outcome.message.c_str();
            EXPECT_EQ(outcome.result, String(resultJson.data(), resultJson.size())) << String(method).c_str();
        }

    }  // namespace

    // -------------------------------------------------------------------------
    // A call that runs
    // -------------------------------------------------------------------------

    TEST(MethodCallTest, ACallChangesTheObjectAndReturnsItsResultUnderResult) {
        McCounter counter;
        const TypedRef ref = Ref(counter);

        ExpectRan(ref, "Add", R"({"amount":5,"label":"abc"})", R"({"result":8})");
        EXPECT_EQ(counter.total, 8);
        EXPECT_EQ(counter.log, "abc");

        ExpectRan(ref, "Reset", "{}", "{}");
        EXPECT_EQ(counter.total, 0);
        EXPECT_EQ(counter.log, "");
    }

    TEST(MethodCallTest, EveryKindOfParameterAndResultIsCarried) {
        McCounter counter;
        const TypedRef ref = Ref(counter);

        ExpectRan(ref, "Widths", R"({"a":-1,"b":2,"c":-3,"d":4,"e":5,"f":-6,"g":7})", R"({"result":8})");
        ExpectRan(ref, "Widths", R"({"a":127,"b":255,"c":-32768,"d":65535,"e":4000000000,"f":-9223372036854775808,"g":9223372036854775807})", R"({"result":4000033148})");
        ExpectRan(ref, "Scale", R"({"factor":1.5,"offset":0.25})", R"({"result":3.25})");
        ExpectRan(ref, "Next", R"({"mode":"Fast","flag":true})", R"({"result":"Fast"})");
        ExpectRan(ref, "Next", R"({"mode":"Fast","flag":false})", R"({"result":"Off"})");
        McCounter fresh;
        ExpectRan(Ref(fresh), "Sum", R"({"values":[1,2,3]})", R"({"result":6})");
        ExpectRan(ref, "Shift", R"({"by":{"x":1,"y":2},"extra":{"x":10,"y":20}})", R"({"result":{"x":11,"y":22}})");
        ExpectRan(ref, "Range", R"({"count":3})", R"({"result":[0,1,2]})");
        ExpectRan(ref, "Describe", "{}", R"({"result":""})");
        counter.log = "text";
        ExpectRan(ref, "Describe", "{}", R"({"result":"text"})");
        ExpectRan(ref, "IsEmpty", "{}", R"({"result":false})");
    }

    TEST(MethodCallTest, AnOptionalParameterMayBeLeftOutAndAnEmptyResultWritesNothing) {
        McCounter counter;
        const TypedRef ref = Ref(counter);

        ExpectRan(ref, "Pick", R"({"index":4})", R"({"result":4})");
        ExpectRan(ref, "Pick", "{}", "{}");
        EXPECT_EQ(counter.total, 2);
    }

    TEST(MethodCallTest, AParameterWithNoNameIsCalledArgN) {
        McCounter counter;
        const TypedRef ref = Ref(counter);
        ExpectRan(ref, "Multiply", R"({"arg0":1,"arg1":2})", R"({"result":1})");
        ExpectRefused(ref, "Multiply", R"({"arg0":1})", CallStatus::bad_arguments, "arg1");
    }

    TEST(MethodCallTest, TheMethodRunsOnceAndOnlyWhenEveryArgumentLoaded) {
        McCounter counter;
        const TypedRef ref = Ref(counter);
        const Outcome outcome = Call(ref, "Scale", R"({"factor":1.0,"offset":0.0})");
        EXPECT_TRUE(outcome.ran);
        EXPECT_EQ(counter.total, 1) << "one call";
    }

    // -------------------------------------------------------------------------
    // The arguments that are refused
    // -------------------------------------------------------------------------

    TEST(MethodCallTest, ABadArgumentIsRefusedWithItsNameAndTheObjectIsUnchanged) {
        McCounter counter;
        counter.total = 40;
        const TypedRef ref = Ref(counter);

        ExpectRefused(ref, "Add", R"({"label":"x"})", CallStatus::bad_arguments, "Add: argument 'amount': Argument is missing");
        ExpectRefused(ref, "Add", R"({"amount":1})", CallStatus::bad_arguments, "argument 'label': Argument is missing");
        ExpectRefused(ref, "Add", R"({"amount":"5","label":"x"})", CallStatus::bad_arguments, "argument 'amount': Invalid integer value");
        ExpectRefused(ref, "Add", R"({"amount":1.5,"label":"x"})", CallStatus::bad_arguments, "argument 'amount'");
        ExpectRefused(ref, "Add", R"({"amount":3000000000,"label":"x"})", CallStatus::bad_arguments, "out of range");
        ExpectRefused(ref, "Add", R"({"amount":1,"label":5})", CallStatus::bad_arguments, "argument 'label': Invalid string value");
        ExpectRefused(ref, "Add", R"({"amount":null,"label":"x"})", CallStatus::bad_arguments, "argument 'amount'");
        ExpectRefused(ref, "Add", R"({"amount":1,"label":"x","extra":2})", CallStatus::bad_arguments, "Add: Unknown argument 'extra'");
        ExpectRefused(ref, "Add", R"({"amount":1,"lable":"x"})", CallStatus::bad_arguments, "did you mean 'label'");
        ExpectRefused(ref, "Widths", R"({"a":300,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7})", CallStatus::bad_arguments, "argument 'a'");
        ExpectRefused(ref, "Widths", R"({"a":1,"b":-2,"c":3,"d":4,"e":5,"f":6,"g":7})", CallStatus::bad_arguments, "argument 'b'");
        ExpectRefused(ref, "Next", R"({"mode":"Turbo","flag":true})", CallStatus::bad_arguments, "argument 'mode'");
        ExpectRefused(ref, "Next", R"({"mode":"Slow","flag":1})", CallStatus::bad_arguments, "argument 'flag'");
        ExpectRefused(ref, "Sum", R"({"values":[1,"x",3]})", CallStatus::bad_arguments, "values[1]");
        ExpectRefused(ref, "Shift", R"({"by":{"x":1,"y":"no"},"extra":{}})", CallStatus::bad_arguments, "by.y");
        ExpectRefused(ref, "Pick", R"({"index":"x"})", CallStatus::bad_arguments, "argument 'index'");

        EXPECT_EQ(counter.total, 40);
        EXPECT_EQ(counter.log, "");
    }

    TEST(MethodCallTest, TheFirstErrorsAreQuotedAndTheRestCounted) {
        McCounter counter;
        const Outcome outcome = Call(Ref(counter), "Add", "{}");
        EXPECT_EQ(outcome.status, CallStatus::bad_arguments);
        EXPECT_TRUE(Contains(outcome.message, "argument 'amount': Argument is missing; argument 'label': Argument is missing")) << outcome.message.c_str();

        const Outcome many = Call(Ref(counter), "Widths", "{}");
        EXPECT_TRUE(Contains(many.message, "(and 4 more)")) << many.message.c_str();
    }

    TEST(MethodCallTest, ArgumentsThatAreNotAnObjectAreRefusedWhenAnArgumentIsNeeded) {
        McCounter counter;
        const TypedRef ref = Ref(counter);
        ExpectRefused(ref, "Add", "[]", CallStatus::bad_arguments, "argument 'amount'");
        ExpectRefused(ref, "Add", "null", CallStatus::bad_arguments, "argument 'amount'");
        ExpectRan(ref, "Reset", "[]", "{}");
    }

    TEST(MethodCallTest, ArgumentsThatAlreadyCarryAnErrorAreRefusedBeforeTheMethodRuns) {
        McCounter counter;
        const Json arguments = Json::object();
        JsonDeserializer reader(arguments);
        reader.ReportError("earlier", "Something went wrong");
        JsonSerializer writer;
        const auto outcome = CallMethod(Ref(counter), "Reset", reader, writer);
        ASSERT_FALSE(outcome.has_value());
        EXPECT_EQ(outcome.error().status, CallStatus::bad_arguments);
        EXPECT_TRUE(Contains(outcome.error().message, "earlier")) << outcome.error().message.c_str();
    }

    TEST(MethodCallTest, TheSettingsOfTheReaderComeBackAfterTheCall) {
        const Json arguments = Json::object();
        McCounter counter;
        {
            JsonDeserializer reader(arguments);
            JsonSerializer writer;
            reader.SetDiagnostics(DiagnosticsMode::Warn);
            ASSERT_TRUE(CallMethod(Ref(counter), "Reset", reader, writer).has_value());
            EXPECT_EQ(reader.GetDiagnostics(), DiagnosticsMode::Warn);
            EXPECT_TRUE(reader.IsStrictValues());
        }
        {
            XmlDeserializer reader;
            XmlSerializer writer;
            ASSERT_TRUE(reader.LoadFromString("<root><args/></root>"));
            ASSERT_TRUE(reader.BeginObject("args"));
            writer.BeginObject("result");
            EXPECT_FALSE(reader.IsStrictValues());
            ASSERT_TRUE(CallMethod(Ref(counter), "Reset", reader, writer).has_value());
            EXPECT_FALSE(reader.IsStrictValues());
            EXPECT_EQ(reader.GetDiagnostics(), DiagnosticsMode::Off);
        }
    }

    TEST(MethodCallTest, AnXmlReaderCarriesTheArgumentsToo) {
        McCounter counter;
        XmlDeserializer reader;
        XmlSerializer writer;
        ASSERT_TRUE(reader.LoadFromString(R"(<root><args amount="5" label="ab"/></root>)"));
        ASSERT_TRUE(reader.BeginObject("args"));
        writer.BeginObject("result");
        const auto outcome = CallMethod(Ref(counter), "Add", reader, writer);
        ASSERT_TRUE(outcome.has_value()) << outcome.error().message.c_str();
        writer.EndObject();
        EXPECT_EQ(counter.total, 7);
        EXPECT_TRUE(Contains(writer.SaveToString(), "result=\"7\""));

        XmlDeserializer bad;
        ASSERT_TRUE(bad.LoadFromString(R"(<root><args amount="five" label="ab"/></root>)"));
        ASSERT_TRUE(bad.BeginObject("args"));
        const auto refused = CallMethod(Ref(counter), "Add", bad, writer);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().status, CallStatus::bad_arguments);
        EXPECT_EQ(counter.total, 7);
    }

    // -------------------------------------------------------------------------
    // The other statuses
    // -------------------------------------------------------------------------

    TEST(MethodCallTest, AnUnknownMethodIsRefusedWithItsName) {
        McCounter counter;
        const TypedRef ref = Ref(counter);
        ExpectRefused(ref, "Nope", "{}", CallStatus::unknown_method, "McCounter");
        ExpectRefused(ref, "Nope", "{}", CallStatus::unknown_method, "'Nope'");
        ExpectRefused(ref, "", "{}", CallStatus::unknown_method, "no method");
        ExpectRefused(ref, "add", "{}", CallStatus::unknown_method, "'add'");

        const String longName(500, 'n');
        const Outcome outcome = Call(ref, StringView(longName.data(), longName.size()), "{}");
        EXPECT_EQ(outcome.status, CallStatus::unknown_method);
        EXPECT_LT(outcome.message.size(), 200U);
        EXPECT_TRUE(Contains(outcome.message, "..."));

        const Outcome control = Call(ref, "a\nb\x01", "{}");
        EXPECT_EQ(control.message.find('\n'), String::npos);
        EXPECT_EQ(control.message.find('\x01'), String::npos);

        const Json arguments = Json::object();
        JsonDeserializer reader(arguments);
        JsonSerializer writer;
        const auto nothing = CallMethod(TypedRef{}, "Reset", reader, writer);
        ASSERT_FALSE(nothing.has_value());
        EXPECT_EQ(nothing.error().status, CallStatus::unknown_method);
    }

    TEST(MethodCallTest, AListedMethodThatIsNotCallableIsNeverInvoked) {
        McCounter counter;
        counter.total = 3;
        const TypedRef ref = Ref(counter);
        ExpectRefused(ref, "Initialize", "{}", CallStatus::not_callable, "Initialize");
        ExpectRefused(ref, "Fill", "{}", CallStatus::not_callable, "Fill");
        ExpectRefused(ref, "Self", "{}", CallStatus::not_callable, "Self");
        ExpectRefused(ref, "Adopt", R"({"object":{}})", CallStatus::not_callable, "Adopt");
        ExpectRefused(ref, "Held", "{}", CallStatus::not_callable, "Held");
        EXPECT_EQ(counter.total, 3);
    }

    TEST(MethodCallTest, ANonConstMethodOfAConstObjectIsRefusedAndAConstMethodRuns) {
        McCounter counter;
        counter.total = 3;
        const McCounter& constCounter = counter;
        const TypedRef ref = TypedRef::Of(constCounter);
        ASSERT_TRUE(ref);
        ASSERT_TRUE(ref.isConst);

        ExpectRefused(ref, "Reset", "{}", CallStatus::const_object, "Reset");
        ExpectRefused(ref, "Add", R"({"amount":1,"label":"x"})", CallStatus::const_object, "Add");
        EXPECT_EQ(counter.total, 3);
        ExpectRan(ref, "IsEmpty", "{}", R"({"result":false})");
        ExpectRan(ref, "Describe", "{}", R"({"result":""})");
        ExpectRefused(ref, "Nope", "{}", CallStatus::unknown_method, "Nope");
    }

    // -------------------------------------------------------------------------
    // The parent chain and the virtual call
    // -------------------------------------------------------------------------

    TEST(MethodCallTest, AMethodOfTheParentIsFoundThroughTheDerivedObject) {
        McDerived derived;
        const TypedRef ref = Ref(derived);
        ExpectRan(ref, "Bump", R"({"by":4})", R"({"result":5})");
        EXPECT_EQ(derived.baseValue, 5);
        EXPECT_EQ(derived.ownValue, 2);
        ExpectRan(ref, "Own", "{}", R"({"result":2})");
        ExpectRefused(ref, "Own", R"({"x":1})", CallStatus::bad_arguments, "Unknown argument 'x'");
    }

    TEST(MethodCallTest, AVirtualMethodRunsTheOverrideWhetherThePointerIsTheBaseOrTheDerivedOne) {
        McDerived derived;
        McBase& asBase = derived;
        const TypedRef throughBase = TypedRef::Of(asBase);
        const TypedRef throughDerived = Ref(derived);
        ASSERT_EQ(throughBase.object, throughDerived.object);

        ExpectRan(throughBase, "Twice", R"({"value":5})", R"({"result":15})");
        ExpectRan(throughDerived, "Twice", R"({"value":5})", R"({"result":15})");
        ExpectRan(throughBase, "Speed", "{}", R"({"result":20})");
        ExpectRan(throughDerived, "Speed", "{}", R"({"result":20})");

        McBase plain;
        ExpectRan(Ref(plain), "Twice", R"({"value":5})", R"({"result":10})");
        ExpectRan(Ref(plain), "Speed", "{}", R"({"result":10})");
    }

    TEST(MethodCallTest, ABaseThatIsNotAtOffsetZeroIsCalledOnItsOwnSubobject) {
        // The generator names a parent only when it is the first base, so the registry is told by hand
        TypeRegistry registry;
        registry.Register(MakeTypeInfo<McBase, void>());
        registry.Register(MakeTypeInfo<McShifted, McBase>());
        McShifted shifted;
        McBase& asBase = shifted;
        const TypedRef ref = TypedRef::Of(asBase, registry);
        ASSERT_TRUE(ref);
        ASSERT_EQ(ref.object, static_cast<void*>(&shifted));
        ASSERT_NE(static_cast<void*>(&asBase), ref.object) << "the base must sit away from the start of the object";

        const MethodRef found = registry.FindMethod(*ref.type, "Bump");
        ASSERT_TRUE(found);
        EXPECT_EQ(found.owner->Name(), "McBase");
        EXPECT_EQ(found.offset, Require(registry.OffsetOfBase(*ref.type, *found.owner)));

        Json arguments = Json::object();
        arguments["by"] = 4;
        JsonDeserializer reader(arguments);
        JsonSerializer writer;
        const auto outcome = CallMethod(ref, "Bump", reader, writer, registry);
        ASSERT_TRUE(outcome.has_value()) << outcome.error().message.c_str();
        EXPECT_EQ(shifted.baseValue, 5);
        EXPECT_EQ(DumpJson(writer.Take()), R"({"result":5})");

        Json twice = Json::object();
        twice["value"] = 5;
        JsonDeserializer twiceReader(twice);
        JsonSerializer twiceWriter;
        ASSERT_TRUE(CallMethod(ref, "Twice", twiceReader, twiceWriter, registry).has_value());
        EXPECT_EQ(DumpJson(twiceWriter.Take()), R"({"result":25})");
    }

    // -------------------------------------------------------------------------
    // FindMethod and invoke
    // -------------------------------------------------------------------------

    TEST(MethodCallTest, FindMethodWalksTheChainAndTheOwnTypeWins) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* derived = registry.Find("McDerived");
        const TypeInfo* base = registry.Find("McBase");
        ASSERT_NE(derived, nullptr);
        ASSERT_NE(base, nullptr);

        const MethodRef own = registry.FindMethod(*derived, "Speed");
        ASSERT_TRUE(own);
        EXPECT_EQ(own.owner, derived);
        EXPECT_EQ(own.offset, 0U);

        const MethodRef inherited = registry.FindMethod(*derived, "Bump");
        ASSERT_TRUE(inherited);
        EXPECT_EQ(inherited.owner, base);

        EXPECT_EQ(registry.FindMethod(*base, "Speed").owner, base);
        EXPECT_FALSE(registry.FindMethod(*base, "Own"));
        EXPECT_FALSE(registry.FindMethod(*derived, "Missing"));
        EXPECT_FALSE(registry.FindMethod(*derived, ""));
    }

    TEST(MethodCallTest, InvokeExistsExactlyWhereCallableIsTrue) {
        size_t methods = 0;
        for (const TypeInfo* info : TypeRegistry::GetInstance().All()) {
            for (const MethodDesc& method : info->methods) {
                ++methods;
                EXPECT_EQ(method.invoke != nullptr, method.callable) << String(info->Name()).c_str() << "::" << String(method.name).c_str();
            }
        }
        EXPECT_GT(methods, 10U);

        const TypeInfo* counter = TypeRegistry::GetInstance().Find("McCounter");
        ASSERT_NE(counter, nullptr);
        EXPECT_FALSE(TypeRegistry::GetInstance().FindMethod(*counter, "Initialize").method->callable);
        EXPECT_TRUE(TypeRegistry::GetInstance().FindMethod(*counter, "Add").method->callable);
    }

}  // namespace BECore::Tests
