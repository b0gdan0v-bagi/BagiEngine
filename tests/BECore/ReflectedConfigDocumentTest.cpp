#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Logger/LogHistorySink.h>
#include <BECore/ReflectedConfigDocumentTypes.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/XmlFieldDiff.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/ShippedConfig.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Generated/ReflectedConfigDocumentTypes.gen.hpp>
#include <Widgets/ConfigEditor/ReflectedConfigDocument.h>

namespace BECore::Tests {

    namespace {

        /// The DOM of a config text, its comments kept as a loaded config keeps them
        struct ConfigDom {
            XmlDocument document;

            explicit ConfigDom(const String& text) {
                EXPECT_TRUE(document.LoadFromString(StringView{text.data(), text.size()}));
            }

            pugi::xml_node Root() {
                return document.GetRoot().GetPugiNode();
            }

            String Text() {
                return XmlFieldDiff::CompactText(Root());
            }
        };

        String Shipped(StringView stem) {
            const String text = ReadShippedConfig(Format("{}.xml", stem));
            EXPECT_FALSE(text.empty()) << String(stem).c_str();
            return text;
        }

        /// @p text with the first occurrence of @p from replaced by @p to
        String With(String text, const char* from, const char* to) {
            const size_t at = text.find(from);
            EXPECT_NE(at, String::npos) << from;
            if (at != String::npos) {
                text.replace(at, String(from).size(), to);
            }
            return text;
        }

        /// The index of the first element of @p list whose type is @p type
        size_t IndexOf(const ReflectedConfigDocument::List& list, StringView type) {
            for (size_t i = 0; i < list.elements.size(); ++i) {
                if (list.elements[i].type->Name() == type) {
                    return i;
                }
            }
            ADD_FAILURE() << "no element of the type " << String(type).c_str();
            return 0;
        }

        template <typename T>
        T& ObjectAt(const ReflectedConfigDocument& document, size_t element) {
            return *static_cast<T*>(At(At(document.GetLists(), 0).elements, element).object.Get());
        }

        const TypeInfo& TypeOf(StringView name) {
            return Require(TypeRegistry::GetInstance().Find(name));
        }

        bool Offers(const ReflectedConfigDocument::List& list, StringView type) {
            return eastl::any_of(list.types.begin(), list.types.end(), [type](const TypeInfo* candidate) { return candidate->Name() == type; });
        }

        /// Why @p text does not open as the config @p stem; empty when it does
        String Refusal(StringView stem, const String& text, const ContentCookRegistry& registry = ContentCookRegistry::GetInstance()) {
            ConfigDom dom(text);
            const auto opened = ReflectedConfigDocument::Open(stem, dom.Root(), registry);
            return opened ? String() : opened.error();
        }

        bool Mentions(const String& text, const char* part) {
            return text.find(part) != String::npos;
        }

    }  // namespace

    TEST(ReflectedConfigDocumentTest, AnApplyWithoutAnEditLeavesTheShippedConfigsAsTheyWere) {
        for (const StringView stem : {StringView{"LoggerConfig"}, StringView{"AssertHandlersConfig"}, StringView{"ApplicationConfig"}, StringView{"AudioConfig"}}) {
            SCOPED_TRACE(String(stem).c_str());
            ConfigDom dom(Shipped(stem));
            const String before = dom.Text();

            auto document = ReflectedConfigDocument::Open(stem, dom.Root());
            ASSERT_TRUE(document.has_value()) << document.error().c_str();
            document->NoteChange();
            EXPECT_FALSE(document->IsDirty());
            document->Apply();

            EXPECT_EQ(dom.Text(), before);
        }
    }

    TEST(ReflectedConfigDocumentTest, AnEditChangesItsFieldOnlyAndAnEditUndoneIsNoChange) {
        ConfigDom dom(Shipped("LoggerConfig"));
        const String before = dom.Text();
        auto opened = ReflectedConfigDocument::Open("LoggerConfig", dom.Root());
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();
        ReflectedConfigDocument& document = *opened;
        const size_t console = IndexOf(At(document.GetLists(), 0), "ConsoleSink");
        // The disabled sink is an object like the others
        EXPECT_FALSE(At(At(document.GetLists(), 0).elements, console).enabled);
        auto& sink = ObjectAt<ConsoleSink>(document, console);

        sink.SetPriority(3);
        document.NoteChange();
        EXPECT_TRUE(document.IsDirty());
        sink.SetPriority(0);
        document.NoteChange();
        EXPECT_FALSE(document.IsDirty());
        sink.SetPriority(3);
        document.NoteChange();
        document.Apply();

        EXPECT_FALSE(document.IsDirty());
        EXPECT_EQ(dom.Text(), With(before, R"(type="ConsoleSink" enabled="false" priority="0")", R"(type="ConsoleSink" enabled="false" priority="3")"));
    }

    TEST(ReflectedConfigDocumentTest, AFieldTheFileLeavesToItsDefaultAppearsOnlyWhenEdited) {
        ConfigDom dom(Shipped("ApplicationConfig"));
        auto opened = ReflectedConfigDocument::Open("ApplicationConfig", dom.Root());
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();
        ReflectedConfigDocument& document = *opened;
        const ReflectedConfigDocument::Object& root = Require(document.GetRoot());
        ASSERT_FALSE(dom.Root().attribute("manualClock"));
        const String before = dom.Text();

        auto& config = *static_cast<ApplicationConfig*>(root.object.Get());
        config._manualClock = true;
        config._clearColor.r = 1;
        document.NoteChange();
        document.Apply();

        String expected = With(before, R"(mode="Editor")", R"(mode="Editor" manualClock="true")");
        expected = With(expected, R"(<clearColor r="30")", R"(<clearColor r="1")");
        EXPECT_EQ(dom.Text(), expected);
    }

    TEST(ReflectedConfigDocumentTest, EnabledOfAnElementIsStagedAndSaved) {
        ConfigDom dom(Shipped("LoggerConfig"));
        const String before = dom.Text();
        auto opened = ReflectedConfigDocument::Open("LoggerConfig", dom.Root());
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();
        ReflectedConfigDocument& document = *opened;
        const size_t console = IndexOf(At(document.GetLists(), 0), "ConsoleSink");

        document.SetEnabled(0, console, true);
        EXPECT_TRUE(document.IsDirty());
        document.SetEnabled(0, console, false);
        EXPECT_FALSE(document.IsDirty());
        document.SetEnabled(0, console, true);
        EXPECT_EQ(dom.Text(), before);
        document.Apply();

        EXPECT_EQ(dom.Text(), With(before, R"(type="ConsoleSink" enabled="false")", R"(type="ConsoleSink" enabled="true")"));
    }

    TEST(ReflectedConfigDocumentTest, AReflectedEnabledFieldIsTheOneControlOfTheAttribute) {
        ConfigDom dom(Shipped("AssertHandlersConfig"));
        const String before = dom.Text();
        auto opened = ReflectedConfigDocument::Open("AssertHandlersConfig", dom.Root());
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();
        ReflectedConfigDocument& document = *opened;
        const size_t breaker = IndexOf(At(document.GetLists(), 0), "DebugBreakHandler");
        EXPECT_TRUE(At(At(document.GetLists(), 0).elements, breaker).hasEnabledField);

        ObjectAt<DebugBreakHandler>(document, breaker).SetEnabled(false);
        document.NoteChange();
        EXPECT_TRUE(document.IsDirty());
        document.Apply();

        EXPECT_EQ(dom.Text(), With(before, R"(type="DebugBreakHandler" enabled="true")", R"(type="DebugBreakHandler" enabled="false")"));
        const auto reopened = ReflectedConfigDocument::Open("AssertHandlersConfig", dom.Root());
        ASSERT_TRUE(reopened.has_value()) << reopened.error().c_str();
        EXPECT_FALSE(ObjectAt<DebugBreakHandler>(*reopened, breaker).IsEnabled());
    }

    TEST(ReflectedConfigDocumentTest, AChangedTypeIsSavedWithTheAttributesItDoesNotRead) {
        ConfigDom dom(Shipped("LoggerConfig"));
        const String before = dom.Text();
        auto opened = ReflectedConfigDocument::Open("LoggerConfig", dom.Root());
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();
        ReflectedConfigDocument& document = *opened;
        const ReflectedConfigDocument::List& sinks = At(document.GetLists(), 0);
        const size_t file = IndexOf(sinks, "FileSink");

        document.SetEnabled(0, file, false);
        ASSERT_TRUE(document.ChangeType(0, file, TypeOf("ConsoleSink")).has_value());
        EXPECT_EQ(At(sinks.elements, file).type->Name(), "ConsoleSink");
        // What is staged beside the object stays
        EXPECT_FALSE(At(sinks.elements, file).enabled);
        ASSERT_TRUE(document.ChangeType(0, file, TypeOf("FileSink")).has_value());
        document.SetEnabled(0, file, true);
        EXPECT_FALSE(document.IsDirty());
        ASSERT_TRUE(document.ChangeType(0, file, TypeOf("ConsoleSink")).has_value());
        EXPECT_TRUE(document.IsDirty());
        document.Apply();

        EXPECT_EQ(dom.Text(), With(before, R"(type="FileSink")", R"(type="ConsoleSink")"));
    }

    TEST(ReflectedConfigDocumentTest, ATypeThatCannotReadTheElementIsRefusedAndTheElementStays) {
        ConfigDom dom(R"(<root><sinks><sink type="ConsoleSink" capacity="many"/></sinks></root>)");
        auto opened = ReflectedConfigDocument::Open("LoggerConfig", dom.Root());
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();
        ReflectedConfigDocument& document = *opened;

        const auto changed = document.ChangeType(0, 0, TypeOf("LogHistorySink"));

        ASSERT_FALSE(changed.has_value());
        EXPECT_TRUE(Mentions(changed.error(), "many")) << changed.error().c_str();
        EXPECT_EQ(At(At(document.GetLists(), 0).elements, 0).type->Name(), "ConsoleSink");
        EXPECT_FALSE(document.IsDirty());
    }

    TEST(ReflectedConfigDocumentTest, OpeningAndChangingATypeStartNoSink) {
        const TestTempFolder folder("BagiReflectedConfigDocumentTest");
        const String logFile = folder.PathOf("sink.log");
        ConfigDom dom(Format(R"(<root><sinks><sink type="FileSink" filename="{0}"/><sink type="LogHistorySink"/><sink type="OutputSink" filename="{0}"/></sinks></root>)", logFile));

        auto opened = ReflectedConfigDocument::Open("LoggerConfig", dom.Root());
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();
        ReflectedConfigDocument& document = *opened;
        ASSERT_TRUE(document.ChangeType(0, 2, TypeOf("FileSink")).has_value());

        // Initialize() opens the file of a FileSink, makes the history of a LogHistorySink and subscribes either to the log
        EXPECT_FALSE(PathExists(Path(StringView{logFile.data(), logFile.size()})));
        EXPECT_EQ(ObjectAt<LogHistorySink>(document, 1).GetHistory(), nullptr);
    }

    TEST(ReflectedConfigDocumentTest, AConfigItCannotHoldIsRefusedWithTheReason) {
        const String unknown = Refusal("LoggerConfig", R"(<root><sinks><sink type="Console"/></sinks></root>)");
        EXPECT_TRUE(Mentions(unknown, "unknown type 'Console'")) << unknown.c_str();

        const String untyped = Refusal("LoggerConfig", R"(<root><sinks><sink priority="1"/></sinks></root>)");
        EXPECT_TRUE(Mentions(untyped, "no 'type'")) << untyped.c_str();

        // A disabled element is read too, and strictly
        const String malformed = Refusal("LoggerConfig", R"(<root><sinks><sink type="ConsoleSink" enabled="false" priority="high"/></sinks></root>)");
        EXPECT_TRUE(Mentions(malformed, "sinks[0]") && Mentions(malformed, "high")) << malformed.c_str();

        const String rootValue = Refusal("ApplicationConfig", R"(<root mode="Editr"/>)");
        EXPECT_TRUE(Mentions(rootValue, "Editr")) << rootValue.c_str();

        EXPECT_FALSE(Refusal("SceneConfig", "<root/>").empty());
        EXPECT_FALSE(Refusal("TextureLibrary", "<root/>").empty());
    }

    TEST(ReflectedConfigDocumentTest, TheOwnFieldsOfARootStandBesideItsList) {
        ContentCookRegistry registry;
        const CookableConfig<ProbeDocumentRoot> cookable{"ProbeDocument", false, registry};
        ConfigDom dom(R"(<root limit="5"><items><item type="ProbeDocumentLeaf" weight="2"/></items></root>)");
        const String before = dom.Text();
        auto opened = ReflectedConfigDocument::Open("ProbeDocument", dom.Root(), registry);
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();
        ReflectedConfigDocument& document = *opened;
        auto& root = *static_cast<ProbeDocumentRoot*>(Require(document.GetRoot()).object.Get());
        EXPECT_EQ(root.limit, 5);
        EXPECT_TRUE(root.items.empty());

        root.limit = 6;
        ObjectAt<ProbeDocumentLeaf>(document, 0).weight = 4;
        document.NoteChange();
        document.Apply();

        EXPECT_EQ(dom.Text(), With(With(before, R"(limit="5")", R"(limit="6")"), R"(weight="2")", R"(weight="4")"));
        // An element that has a factory list of its own would be initialized by the reflected load of that list
        EXPECT_FALSE(Offers(At(document.GetLists(), 0), "ProbeDocumentNest"));
        EXPECT_FALSE(Refusal("ProbeDocument", R"(<root><items><item type="ProbeDocumentNest"/></items></root>)", registry).empty());
    }

}  // namespace BECore::Tests
