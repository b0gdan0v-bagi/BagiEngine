// A script as a field of a content type (Values/Effects/EffectScriptAccessor.h): `<onTick script="..."/>` read from XML, binary and JSON, saved again, and the line a mistake gives. The
// document is `EffectsDocument` of EffectsDocument.h (its generated code is included here); the context objects are those of ValueTestTypes.h and ProgramDomainTypes.h.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/EffectsDocument.h>
#include <BECore/EffectsToys.h>
#include <BECore/ProgramDomainTypes.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Threading/Thread.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Effects/WriteTargets.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Common/Printers.h>
#include <Common/Wait.h>
#include <Generated/EffectsDocument.gen.hpp>
#include <atomic>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace EffectsData;
    using namespace ValueTestData;
    using ProgramDomainData::DomainBoard;

    namespace {

        constexpr StringView kState = "State";
        constexpr StringView kDocumentName = "units/trigger.xml";
        constexpr StringView kContext = R"(<context caster="ValueUnitStats" cast="ValueCastInfo" board="DomainBoard"/>)";
        constexpr StringView kTick = "if (caster.attack > 10) { caster.attack -= 1; emit FxCount(caster.attack); } else { caster.alive = false; }";
        constexpr StringView kExpire = "caster.score = 7; board.turn++;";
        constexpr int kLoadingThreads = 2;
        constexpr int kLoadRounds = 50;

        bool HasText(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        template <typename Reader>
        String ReportsOf(const Reader& reader) {
            String out;
            for (const Vector<DeserializeError>* list : {&reader.GetErrors(), &reader.GetWarnings()}) {
                for (const DeserializeError& report : *list) {
                    AppendFormat(out, "{}: {}\n", report.path, report.errorMessage);
                }
            }
            return out;
        }

        /// What a host declares before its content loads: the caster and the board are writable, the cast is not
        struct Host {
            WriteTargets targets;

            Host() {
                (void)RegisterEffectToys();
                EXPECT_TRUE(targets.Allow("caster"));
                EXPECT_TRUE(targets.Allow("board"));
            }
        };

        String LoadXmlDocument(const String& xml, EffectsDocument& target) {
            XmlDeserializer reader;
            EXPECT_TRUE(reader.LoadFromString(xml)) << xml.c_str();
            EXPECT_TRUE(reader.BeginObject(kState));
            target.Deserialize(reader);
            reader.EndObject();
            return ReportsOf(reader);
        }

        /// Loads `<root><State>@p children</State></root>` into @p target; the caller has the link scope open. Every error and warning, one text
        String LoadXml(StringView children, EffectsDocument& target) {
            return LoadXmlDocument(Format("<root><State>{}</State></root>", children), target);
        }

        String SaveXml(const EffectsDocument& value) {
            XmlSerializer serializer;
            if (serializer.BeginObject(kState)) {
                value.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        String LoadBinary(const EffectsDocument& source, EffectsDocument& target) {
            BinarySerializer serializer;
            if (serializer.BeginObject(kState)) {
                source.Serialize(serializer);
                serializer.EndObject();
            }
            BinaryDeserializer reader;
            EXPECT_TRUE(reader.LoadFromBuffer(serializer.GetData()));
            EXPECT_TRUE(reader.BeginObject(kState));
            target.Deserialize(reader);
            reader.EndObject();
            return ReportsOf(reader);
        }

        String Elements(StringView tick, StringView expire) {
            return Format("{}<onTick script=\"{}\"/><onExpire script=\"{}\"/>", kContext, tick, expire);
        }

        /// The objects the loaded scripts run over
        struct World {
            ContextDecl declaration;
            ValueUnitStats caster;
            ValueCastInfo cast;
            DomainBoard board;
            Context context{declaration};
            EmitBufferStorage<4> storage;

            World() {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
                EXPECT_TRUE(declaration.Add<DomainBoard>("board").has_value());
                context.SetMutable(0, caster);
                context.Set(1, cast);
                context.SetMutable(2, board);
            }

            World(const World&) = delete;
            World& operator=(const World&) = delete;
        };

        void ExpectScriptsRunLikePlainCode(const EffectsDocument& document) {
            const EffectsData::EffectToys& toys = RegisterEffectToys();
            World world;
            world.caster.attack = 12;
            const EffectResult tick = document.onTick.Run(world.context, world.storage.Buffer());
            EXPECT_EQ(world.caster.attack, 11);
            EXPECT_TRUE(world.caster.alive);
            EXPECT_EQ(tick.emitted, 1U);
            ASSERT_EQ(world.storage.Buffer().Count(), 1U);
            EXPECT_EQ(world.storage.Buffer().Commands()[0].id, toys.count);
            EXPECT_EQ(world.storage.Buffer().Commands()[0].Argument<int32_t>(0), 11);

            world.caster.attack = 10;
            static_cast<void>(document.onTick.Run(world.context, world.storage.Buffer()));
            EXPECT_FALSE(world.caster.alive);
            EXPECT_EQ(world.caster.attack, 10);

            static_cast<void>(document.onExpire.Run(world.context, world.storage.Buffer()));
            EXPECT_EQ(world.caster.score, 7);
            EXPECT_EQ(world.board.turn, 1);
        }

        void ExpectUntouched(const EffectsDocument& document) {
            EXPECT_TRUE(document.onTick.Empty());
            EXPECT_TRUE(document.onExpire.Empty());
        }

    }  // namespace

    TEST(EffectsLoadTest, ScriptsLoadFromXmlAndRunLikePlainCode) {
        Host host;
        EffectsDocument document;
        ValueLinkScope scope(kDocumentName);
        scope.UseWriteTargets(host.targets);
        const String reports = LoadXml(Elements(kTick, kExpire), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        EXPECT_EQ(document.onTick.Source(), kTick);
        ExpectScriptsRunLikePlainCode(document);
    }

    TEST(EffectsLoadTest, AScriptSavesItsTextAndLoadsAgainEqualInXmlBinaryAndJson) {
        Host host;
        EffectsDocument document;
        {
            ValueLinkScope scope(kDocumentName);
            scope.UseWriteTargets(host.targets);
            ASSERT_TRUE(LoadXml(Elements(kTick, kExpire), document).empty());
        }
        const String saved = SaveXml(document);
        EXPECT_TRUE(HasText(saved, "<onExpire script=\"caster.score = 7; board.turn++;\"")) << saved.c_str();

        EffectsDocument fromXml;
        {
            ValueLinkScope scope(kDocumentName);
            scope.UseWriteTargets(host.targets);
            const String reports = LoadXmlDocument(saved, fromXml);
            EXPECT_TRUE(reports.empty()) << reports.c_str();
        }
        EXPECT_EQ(fromXml.onTick, document.onTick);
        EXPECT_EQ(fromXml.onExpire, document.onExpire);
        ExpectScriptsRunLikePlainCode(fromXml);

        EffectsDocument fromBinary;
        {
            ValueLinkScope scope(kDocumentName);
            scope.UseWriteTargets(host.targets);
            const String reports = LoadBinary(document, fromBinary);
            EXPECT_TRUE(reports.empty()) << reports.c_str();
        }
        EXPECT_EQ(fromBinary.onTick, document.onTick);
        ExpectScriptsRunLikePlainCode(fromBinary);

        JsonSerializer serializer;
        document.Serialize(serializer);
        const Json json = serializer.Take();
        EffectsDocument fromJson;
        {
            ValueLinkScope scope(kDocumentName);
            scope.UseWriteTargets(host.targets);
            JsonDeserializer reader(json);
            fromJson.Deserialize(reader);
            EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        }
        EXPECT_EQ(fromJson.onTick, document.onTick);
        ExpectScriptsRunLikePlainCode(fromJson);
    }

    TEST(EffectsLoadTest, JsonReadsTheObjectWithAScript) {
        Host host;
        const auto parsed = ParseJson(R"x({"context": [{"name": "caster", "type": "ValueUnitStats"}, {"name": "cast", "type": "ValueCastInfo"}, {"name": "board", "type": "DomainBoard"}],
                                          "onTick": {"script": "if (caster.attack > 10) { caster.attack -= 1; emit FxCount(caster.attack); } else { caster.alive = false; }"},
                                          "onExpire": {"script": "caster.score = 7; board.turn++;"}})x");
        ASSERT_TRUE(parsed.has_value());
        EffectsDocument document;
        ValueLinkScope scope(kDocumentName);
        scope.UseWriteTargets(host.targets);
        JsonDeserializer reader(*parsed);
        document.Deserialize(reader);
        EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        ExpectScriptsRunLikePlainCode(document);
    }

    TEST(EffectsLoadTest, AnEmptyScriptIsAllowedRunsNothingAndSavesAsEmpty) {
        Host host;
        EffectsDocument document;
        {
            ValueLinkScope scope(kDocumentName);
            scope.UseWriteTargets(host.targets);
            const String reports = LoadXml(Elements("", kExpire), document);
            EXPECT_TRUE(reports.empty()) << reports.c_str();
        }
        EXPECT_TRUE(document.onTick.Empty());
        EXPECT_FALSE(document.onExpire.Empty());
        World world;
        const EffectResult result = document.onTick.Run(world.context, world.storage.Buffer());
        EXPECT_EQ(result.stores, 0U);
        EXPECT_EQ(result.emitted, 0U);
        EXPECT_TRUE(HasText(SaveXml(document), "<onTick script=\"\"")) << SaveXml(document).c_str();
    }

    TEST(EffectsLoadTest, AScriptWithoutTheKeysOfItsOwnFormIsReadAsAnErrorAndTheFieldStays) {
        Host host;
        struct Mistake {
            StringView children;
            StringView path;
            StringView words;
        };
        const Mistake mistakes[] = {
            {R"x(<onTick/>)x", "State.onTick.script", "needs its statements in 'script'"},
            {R"x(<onTick script="caster.attack = 1;" from="caster.attack"/>)x", "State.onTick.from", "'from' is there beside it"},
            {R"x(<onTick script="caster.attack = 1;" expr="1 + 2"/>)x", "State.onTick.expr", "'expr' is there beside it"},
            {R"x(<onTick script="caster.attack = 1;" value="3"/>)x", "State.onTick.value", "'value' is there beside it"},
            {R"x(<onTick script="caster.attack = ;"/>)x", "State.onTick.script", "column "},
            {R"x(<onTick script="caster.attack = 1"/>)x", "State.onTick.script", "column 18: expected ';'"},
            // A level the host did not allow, a component and a field no script can write name the path
            {R"x(<onTick script="cast.level = 1;"/>)x", "State.onTick.script", "cast.level"},
            {R"x(<onTick script="caster.position.x = 1.0;"/>)x", "State.onTick.script", "whole field"},
            {R"x(<onTick script="caster.bounds = caster.bounds;"/>)x", "State.onTick.script", "caster.bounds"},
            {R"x(<onTick script="emit NoSuchCommand();"/>)x", "State.onTick.script", "NoSuchCommand"},
        };
        for (const Mistake& mistake : mistakes) {
            EffectsDocument document;
            ValueLinkScope scope(kDocumentName);
            scope.UseWriteTargets(host.targets);
            const String reports = LoadXml(Format("{}{}", kContext, mistake.children), document);
            EXPECT_TRUE(HasText(reports, mistake.path)) << String(mistake.children).c_str() << ": " << reports.c_str();
            EXPECT_TRUE(HasText(reports, mistake.words)) << String(mistake.children).c_str() << ": " << reports.c_str();
            ExpectUntouched(document);
        }
    }

    TEST(EffectsLoadTest, ACompileErrorReadsColumnMessageAndDocument) {
        Host host;
        EffectsDocument document;
        ValueLinkScope scope(kDocumentName);
        scope.UseWriteTargets(host.targets);
        const String reports = LoadXml(Format("{}<onTick script=\"caster.attack = 1; caster.attak = 2;\"/>", kContext), document);
        EXPECT_TRUE(HasText(reports, "column 20: ")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "assignment to 'caster.attak'")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "did you mean 'attack'")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "(document 'units/trigger.xml')")) << reports.c_str();
    }

    TEST(EffectsLoadTest, ALongMessageKeepsTheDocumentNameInTheReport) {
        Host host;
        EffectsDocument document;
        ValueLinkScope scope(kDocumentName);
        scope.UseWriteTargets(host.targets);
        const String reports = LoadXml(Format("{}<onTick script=\"emit NoSuchCommandWhatsoever(1, 2, 3);\"/>", kContext), document);
        EXPECT_TRUE(HasText(reports, "(document 'units/trigger.xml')")) << reports.c_str();
    }

    TEST(EffectsLoadTest, AScriptWithNoContextOrNoTargetsInScopeIsALoadError) {
        Host host;
        {
            // A document that declares no context
            EffectsDocument document;
            ValueLinkScope scope(kDocumentName);
            scope.UseWriteTargets(host.targets);
            const String reports = LoadXml(R"x(<onTick script="caster.attack = 1;"/>)x", document);
            EXPECT_TRUE(HasText(reports, "No context is declared where this script is read")) << reports.c_str();
            EXPECT_TRUE(HasText(reports, "units/trigger.xml")) << reports.c_str();
            ExpectUntouched(document);
        }
        {
            // The host declared no write targets: even a script that writes nothing is refused, the document is not ready for scripts
            EffectsDocument document;
            ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(Format("{}<onTick script=\"emit FxPing();\"/>", kContext), document);
            EXPECT_TRUE(HasText(reports, "no write targets")) << reports.c_str();
            EXPECT_TRUE(HasText(reports, "UseWriteTargets")) << reports.c_str();
            ExpectUntouched(document);
        }
        {
            // An empty set is a decision: nothing is writable, so a store is the error that lists what the host allows (nothing)
            const WriteTargets none;
            EffectsDocument document;
            ValueLinkScope scope(kDocumentName);
            scope.UseWriteTargets(none);
            const String reports = LoadXml(Format("{}<onTick script=\"emit FxPing();\"/><onExpire script=\"caster.attack = 1;\"/>", kContext), document);
            EXPECT_FALSE(HasText(reports, "onTick")) << reports.c_str();
            EXPECT_TRUE(HasText(reports, "State.onExpire.script")) << reports.c_str();
            EXPECT_FALSE(document.onTick.Empty());
            EXPECT_TRUE(document.onExpire.Empty());
        }
    }

    TEST(EffectsLoadTest, TheTargetsOfAnInnerScopeShadowTheOnesAroundIt) {
        Host host;
        WriteTargets narrow;
        ASSERT_TRUE(narrow.Allow("board"));
        ValueLinkScope outer(kDocumentName);
        outer.UseWriteTargets(host.targets);
        EXPECT_EQ(ValueLinkScope::Targets(), &host.targets);
        {
            ValueLinkScope inner("inner.xml");
            EXPECT_EQ(ValueLinkScope::Targets(), &host.targets);
            inner.UseWriteTargets(narrow);
            EXPECT_EQ(ValueLinkScope::Targets(), &narrow);
        }
        EXPECT_EQ(ValueLinkScope::Targets(), &host.targets);
    }

    TEST(EffectsLoadTest, AFailedLoadAddsNothingToThePoolAndKeepsTheOldScript) {
        Host host;
        EffectsDocument document;
        ValueLinkScope scope(kDocumentName);
        scope.UseWriteTargets(host.targets);
        ASSERT_TRUE(LoadXml(Elements(kTick, kExpire), document).empty());
        const EffectScript before = document.onTick;
        const size_t count = ProgramPool::Count();
        // A fresh declaration is not needed: the scope keeps the first `<context>`, and a second one in the same scope is refused, so the elements alone are loaded
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString("<root><State><onTick script=\"caster.attack = caster.attack * 3 + 11 + cast.level; caster.nothing = 1;\"/></State></root>"));
        ASSERT_TRUE(reader.BeginObject(kState));
        document.Deserialize(reader);
        reader.EndObject();
        EXPECT_TRUE(reader.HasErrors());
        EXPECT_EQ(document.onTick, before);
        EXPECT_EQ(ProgramPool::Count(), count);
    }

    TEST(EffectsLoadTest, AKeyThatIsNoPartOfAScriptIsReportedByTheStrictMode) {
        Host host;
        EffectsDocument document;
        ValueLinkScope scope(kDocumentName);
        scope.UseWriteTargets(host.targets);
        XmlDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Error);
        const String xml = Format("<root><State>{}<onTick script=\"caster.attack = 1;\" scrpt=\"x\"/></State></root>", kContext);
        ASSERT_TRUE(reader.LoadFromString(xml));
        ASSERT_TRUE(reader.BeginObject(kState));
        document.Deserialize(reader);
        reader.EndObject();
        reader.FinishLoad();
        const String reports = ReportsOf(reader);
        EXPECT_TRUE(HasText(reports, "State.onTick.scrpt")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "did you mean 'script'")) << reports.c_str();
    }

    TEST(EffectsLoadTest, TwoThreadsLoadingWithTheirOwnTargetsNeverSeeEachOthers) {
        Host host;
        WriteTargets boardOnly;
        ASSERT_TRUE(boardOnly.Allow("board"));
        struct Outcome {
            bool metOther = false;
            bool sawOwn = true;
        };
        std::atomic<int> arrived{0};
        // The caster is writable for the first thread only: the same text loads there and is refused in the other
        const auto work = [&arrived](const WriteTargets& targets, bool casterWritable, Outcome& outcome) {
            arrived.fetch_add(1);
            outcome.metOther = WaitUntil([&arrived] { return arrived.load() == kLoadingThreads; });
            for (int round = 0; round < kLoadRounds && outcome.sawOwn; ++round) {
                ValueLinkScope scope("thread.xml");
                scope.UseWriteTargets(targets);
                EffectsDocument document;
                const String reports = LoadXml(Format("{}<onTick script=\"caster.attack = 1;\"/>", kContext), document);
                outcome.sawOwn = reports.empty() == casterWritable && document.onTick.Empty() != casterWritable && ValueLinkScope::Targets() == &targets;
            }
        };
        Outcome a;
        Outcome b;
        {
            Thread threadA("EffectsLoad A", [&] { work(host.targets, true, a); });
            Thread threadB("EffectsLoad B", [&] { work(boardOnly, false, b); });
            threadA.join();
            threadB.join();
        }
        EXPECT_TRUE(a.metOther);
        EXPECT_TRUE(b.metOther);
        EXPECT_TRUE(a.sawOwn);
        EXPECT_TRUE(b.sawOwn);
        EXPECT_EQ(ValueLinkScope::Targets(), nullptr);
    }

}  // namespace BECore::Tests
