// `MakeFromConfig` opens the link scope of a named config: the document's `<context>` lands in it, the `Value` fields link against it, and the
// log line of a mistake names the config, the field, the path and what the type has. The document type is that of ValueTestTypes.h.

#include <BECore/Config/ConfigFactory.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        // A cached config whose root other tests read; the fixture adds the document's elements to it and takes them away again
        constexpr StringView kConfig = "TestsConfig";
        constexpr StringView kConfigPrefix = "Config 'TestsConfig'";
        // The element of that config that `ValueConfigDocument` does not know: the one finding of a clean document
        constexpr StringView kForeignElement = "'tests'";

        constexpr StringView kContext = R"(<context caster="ValueUnitStats" cast="ValueCastInfo"/>)";
        constexpr StringView kLinkedValues = R"(<damage from="caster.attack"/><power from="cast.power"/><aim x="1" y="2" z="3"/>)";

    }  // namespace

    /// A document is put into the cached config `TestsConfig` for the test, as ConfigDiagnosticsTest does with its stray keys
    class ValueConfigTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            pugi::xml_node root = CoreManager::GetConfigManager().GetConfig(kConfig).GetPugiNode();
            for (const pugi::xml_node& node : _added) {
                root.remove_child(node);
            }
            _added.clear();
        }

        /// Appends the elements of @p fragment to the root of the config
        void Install(StringView fragment) {
            pugi::xml_node root = CoreManager::GetConfigManager().GetConfig(kConfig).GetPugiNode();
            ASSERT_TRUE(root);
            const String wrapped = Format("<fragment>{}</fragment>", fragment);
            pugi::xml_document parsed;
            ASSERT_TRUE(parsed.load_buffer(wrapped.data(), wrapped.size()));
            for (const pugi::xml_node& child : parsed.first_child().children()) {
                _added.push_back(root.append_copy(child));
            }
        }

        Vector<pugi::xml_node> _added;
    };

    TEST_F(ValueConfigTest, ADeclaredContextLoadsTheConstAndTheSlotValues) {
        Install(Format("{}{}", kContext, kLinkedValues));
        LogRecorder recorder;
        const auto loaded = MakeFromConfig<ValueConfigDocument>(kConfig);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Count(LogLevel::Error, {kConfigPrefix}), 0U);
        // Only the element of the config that this type does not know is a finding
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {kConfigPrefix}), recorder.Count(LogLevel::Warning, {kConfigPrefix, kForeignElement}));
        const ValueConfigDocument& document = *loaded;
        ASSERT_EQ(document.context.Size(), 2U);
        ASSERT_TRUE(document.damage.IsSlot());
        ASSERT_TRUE(document.power.IsSlot());
        ASSERT_FALSE(document.aim.IsSlot());

        ValueUnitStats caster;
        ValueCastInfo cast;
        Context context(document.context);
        context.Set(0, caster);
        context.Set(1, cast);
        EXPECT_EQ(&document.damage.Get(context), &caster.attack);
        EXPECT_EQ(&document.power.Get(context), &cast.power);
        EXPECT_EQ(document.aim.Get(context), (Vec3{1.0f, 2.0f, 3.0f}));
    }

    TEST_F(ValueConfigTest, TheScopeIsGoneWhenTheLoadReturns) {
        Install(Format("{}{}", kContext, kLinkedValues));
        const auto loaded = MakeFromConfig<ValueConfigDocument>(kConfig);
        EXPECT_EQ(loaded->context.Size(), 2U);
        EXPECT_EQ(ValueLinkScope::Current(), nullptr);
        EXPECT_TRUE(ValueLinkScope::DocumentName().empty());
    }

    TEST_F(ValueConfigTest, AWrongPathIsOneErrorWithTheConfigTheFieldThePathAndTheFieldsTheTypeHas) {
        Install(Format("{}{}", kContext, R"(<damage from="caster.attak"/><power from="cast.power"/>)"));
        LogRecorder recorder;
        const auto document = MakeFromConfig<ValueConfigDocument>(kConfig);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Count(LogLevel::Error, {kConfigPrefix}), 1U);
        const auto* error = recorder.Find(LogLevel::Error, {kConfigPrefix, "damage", "caster.attak", "ValueUnitStats", "attack", "spawn"});
        ASSERT_NE(error, nullptr);
        EXPECT_FALSE(document->damage.IsSlot());
        EXPECT_EQ(document->context.Size(), 2U);
        EXPECT_TRUE(document->power.IsSlot());  // what could be read is kept
    }

    TEST_F(ValueConfigTest, ATypeMismatchLogsBothTypes) {
        Install(Format("{}{}", kContext, R"(<damage from="cast.power"/>)"));
        LogRecorder recorder;
        const auto document = MakeFromConfig<ValueConfigDocument>(kConfig);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Count(LogLevel::Error, {kConfigPrefix}), 1U);
        const auto* error = recorder.Find(LogLevel::Error, {kConfigPrefix, "damage", "cast.power", "float", "int32"});
        EXPECT_NE(error, nullptr);
        EXPECT_FALSE(document->damage.IsSlot());
    }

    TEST_F(ValueConfigTest, AFromWithNoContextNamesTheConfig) {
        Install(R"(<damage from="caster.attack"/><power value="4"/>)");
        LogRecorder recorder;
        const auto document = MakeFromConfig<ValueConfigDocument>(kConfig);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Count(LogLevel::Error, {kConfigPrefix}), 1U);
        const auto* error = recorder.Find(LogLevel::Error, {kConfigPrefix, "No context is declared", "caster.attack", "(document 'TestsConfig')"});
        EXPECT_NE(error, nullptr);
        EXPECT_FALSE(document->damage.IsSlot());
        EXPECT_FALSE(document->power.IsSlot());  // the literal after it was read
    }

    TEST_F(ValueConfigTest, AnUnknownContextTypeIsAnErrorOfTheConfig) {
        Install(R"(<context caster="ValueUnitStatz"/><damage value="5"/>)");
        LogRecorder recorder;
        const auto document = MakeFromConfig<ValueConfigDocument>(kConfig);
        LogEvent::Flush();

        EXPECT_NE(recorder.Find(LogLevel::Error, {kConfigPrefix, "ValueUnitStatz", "did you mean 'ValueUnitStats'?"}), nullptr);
        EXPECT_EQ(document->context.Size(), 0U);
        EXPECT_EQ(ValueLinkScope::Current(), nullptr);
    }

    TEST_F(ValueConfigTest, TwoLoadsDoNotShareADeclaration) {
        Install(Format("{}{}", kContext, kLinkedValues));
        const auto first = MakeFromConfig<ValueConfigDocument>(kConfig);
        EXPECT_EQ(first->context.Size(), 2U);

        TearDown();
        Install(R"(<damage from="caster.attack"/>)");
        LogRecorder recorder;
        const auto second = MakeFromConfig<ValueConfigDocument>(kConfig);
        LogEvent::Flush();
        EXPECT_NE(recorder.Find(LogLevel::Error, {kConfigPrefix, "No context is declared"}), nullptr);
        EXPECT_EQ(second->context.Size(), 0U);
    }

}  // namespace BECore::Tests
