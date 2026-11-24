#include "CommandLineDeserializerTest.h"

#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Application/CommandLineDeserializer.h>
#include <BECore/Reflection/ReaderLimits.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Generated/CommandLineDeserializerTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using Args = Vector<String>;

        /// A command line, a reader over it and a struct read through it
        template <typename TStruct>
        class Reading {
        public:
            explicit Reading(const Args& args, Span<const StringView> keys = {}) : _deserializer(MakeLine(args), keys) {}

            void Load() {
                _value.Deserialize(_deserializer);
                _deserializer.FinishLoad();
            }

            TStruct& Value() {
                return _value;
            }

            CommandLineDeserializer& Reader() {
                return _deserializer;
            }

            /// The error under @p flag, or null
            const DeserializeError* ErrorAt(StringView flag) const {
                for (const DeserializeError& error : _deserializer.GetErrors()) {
                    if (error.path == flag) {
                        return &error;
                    }
                }
                return nullptr;
            }

        private:
            const CommandLine& MakeLine(const Args& args) {
                _line.Set(args);
                return _line;
            }

            CommandLine _line;
            CommandLineDeserializer _deserializer;
            TStruct _value;
        };

        using ProbeReading = Reading<FlagProbe>;

    }  // namespace

    TEST(CommandLineDeserializerTest, FlagNameIsTheKeyInKebabCase) {
        FlagBuffer buffer;
        EXPECT_EQ(MakeFlagName("controlPort", buffer), "--control-port");
        EXPECT_EQ(MakeFlagName("replayExit", buffer), "--replay-exit");
        EXPECT_EQ(MakeFlagName("headless", buffer), "--headless");
        EXPECT_EQ(MakeFlagName("a1B2", buffer), "--a1-b2");
    }

    TEST(CommandLineDeserializerTest, FlagNameThatDoesNotFitIsEmpty) {
        FlagBuffer buffer;
        const String fits(kMaxFlagBytes - 2, 'a');
        EXPECT_EQ(MakeFlagName(StringView(fits), buffer).size(), kMaxFlagBytes);
        const String tooLong(kMaxFlagBytes - 1, 'a');
        EXPECT_TRUE(MakeFlagName(StringView(tooLong), buffer).empty());
        // The capital grows the text by one byte
        const String grows = String(kMaxFlagBytes - 3, 'a') + "B";
        EXPECT_TRUE(MakeFlagName(StringView(grows), buffer).empty());
    }

    TEST(CommandLineDeserializerTest, AnAbsentFlagLeavesTheFieldAndIsNoError) {
        ProbeReading reading({"--unrelated=1", "--other"});
        reading.Load();
        EXPECT_FALSE(reading.Reader().HasErrors());
        EXPECT_FALSE(reading.Value()._enabled);
        EXPECT_TRUE(reading.Value()._onByDefault);
        EXPECT_EQ(reading.Value()._port, 7);
        EXPECT_EQ(reading.Value()._label, "keep");
    }

    TEST(CommandLineDeserializerTest, ABareBoolFlagIsTrue) {
        ProbeReading reading({"--enabled"});
        reading.Load();
        EXPECT_FALSE(reading.Reader().HasErrors());
        EXPECT_TRUE(reading.Value()._enabled);
    }

    TEST(CommandLineDeserializerTest, ABoolFlagTakesTheSpellingsOfStrictText) {
        const struct {
            const char* text;
            bool expected;
        } cases[] = {{"true", true}, {"1", true}, {"false", false}, {"0", false}};
        for (const auto& item : cases) {
            ProbeReading reading({Format("--enabled={}", item.text), Format("--on-by-default={}", item.text)});
            reading.Value()._enabled = !item.expected;
            reading.Value()._onByDefault = !item.expected;
            reading.Load();
            EXPECT_FALSE(reading.Reader().HasErrors()) << item.text;
            EXPECT_EQ(reading.Value()._enabled, item.expected) << item.text;
            EXPECT_EQ(reading.Value()._onByDefault, item.expected) << item.text;
        }
    }

    TEST(CommandLineDeserializerTest, ABadBoolIsAnErrorNamedByTheFlag) {
        for (const char* text : {"--enabled=yes", "--enabled=", "--enabled=maybe"}) {
            ProbeReading reading({text});
            reading.Load();
            const DeserializeError& error = Require(reading.ErrorAt("--enabled"));
            EXPECT_TRUE(StringView(error.errorMessage).starts_with("Invalid boolean value")) << text;
            EXPECT_FALSE(reading.Value()._enabled) << text;
        }
    }

    TEST(CommandLineDeserializerTest, ABadNumberIsAnErrorNamedByTheFlagAndKeepsTheField) {
        for (const char* text : {"--port=70000", "--port=abc", "--port=-1", "--port=", "--port=1.5"}) {
            ProbeReading reading({text});
            reading.Load();
            const DeserializeError& error = Require(reading.ErrorAt("--port"));
            EXPECT_TRUE(StringView(error.errorMessage).starts_with("Invalid integer value")) << text;
            EXPECT_EQ(reading.Value()._port, 7) << text;
        }
    }

    TEST(CommandLineDeserializerTest, ANumberOutOfRangeSaysSo) {
        ProbeReading reading({"--port=70000"});
        reading.Load();
        EXPECT_TRUE(StringView(Require(reading.ErrorAt("--port")).errorMessage).ends_with("(out of range)"));
    }

    TEST(CommandLineDeserializerTest, ANumberIsReadWholeWithTheSpellingOfStrictText) {
        ProbeReading reading({"--port=+9000", "--level-count=-3", "--scale= 2.5 "});
        reading.Load();
        EXPECT_FALSE(reading.Reader().HasErrors());
        EXPECT_EQ(reading.Value()._port, 9000);
        EXPECT_EQ(reading.Value()._levelCount, -3);
        EXPECT_FLOAT_EQ(reading.Value()._scale, 2.5f);
    }

    TEST(CommandLineDeserializerTest, ABareFlagThatNeedsAValueIsAnError) {
        ProbeReading reading({"--port", "--label", "--title", "--scale"});
        reading.Load();
        for (const StringView flag : {"--port", "--label", "--title", "--scale"}) {
            EXPECT_EQ(Require(reading.ErrorAt(flag)).errorMessage, "needs a value") << String(flag).c_str();
        }
        EXPECT_EQ(reading.Value()._port, 7);
        EXPECT_EQ(reading.Value()._label, "keep");
    }

    TEST(CommandLineDeserializerTest, AnEmptyValueIsAnEmptyString) {
        ProbeReading reading({"--label="});
        reading.Load();
        EXPECT_FALSE(reading.Reader().HasErrors());
        EXPECT_TRUE(reading.Value()._label.empty());
    }

    TEST(CommandLineDeserializerTest, APoolStringFieldIsInterned) {
        ProbeReading reading({"--title=hero"});
        reading.Load();
        EXPECT_FALSE(reading.Reader().HasErrors());
        EXPECT_EQ(reading.Value()._title, PoolString::Intern(StringView("hero")));
    }

    TEST(CommandLineDeserializerTest, APoolStringOverTheLimitIsRefused) {
        const String longName(ReaderLimits::kMaxPoolStringBytes + 1, 'n');
        ProbeReading reading({Format("--title={}", longName)});
        reading.Load();
        EXPECT_TRUE(reading.ErrorAt("--title") != nullptr);
        EXPECT_TRUE(reading.Value()._title.Empty());
    }

    TEST(CommandLineDeserializerTest, ARepeatedFlagTakesItsFirstForm) {
        ProbeReading bareFirst({"--enabled", "--enabled=false"});
        bareFirst.Load();
        EXPECT_TRUE(bareFirst.Value()._enabled);

        ProbeReading valueFirst({"--enabled=false", "--enabled"});
        valueFirst.Value()._enabled = true;
        valueFirst.Load();
        EXPECT_FALSE(valueFirst.Value()._enabled);

        ProbeReading twoValues({"--port=1", "--port=2"});
        twoValues.Load();
        EXPECT_EQ(twoValues.Value()._port, 1);
    }

    TEST(CommandLineDeserializerTest, ALongerFlagIsNotTheFlag) {
        ProbeReading reading({"--portal=1", "--port-extra=2", "--enabled-too"});
        reading.Load();
        EXPECT_FALSE(reading.Reader().HasErrors());
        EXPECT_EQ(reading.Value()._port, 7);
        EXPECT_FALSE(reading.Value()._enabled);
    }

    TEST(CommandLineDeserializerTest, TheKeyListHidesAFieldThatIsNotInIt) {
        const StringView keys[] = {"port"};
        ProbeReading reading({"--port=9", "--enabled", "--label=x"}, keys);
        reading.Load();
        EXPECT_FALSE(reading.Reader().HasErrors());
        EXPECT_EQ(reading.Value()._port, 9);
        EXPECT_FALSE(reading.Value()._enabled);
        EXPECT_EQ(reading.Value()._label, "keep");
        EXPECT_TRUE(reading.Reader().HasKey("port"));
        EXPECT_FALSE(reading.Reader().HasKey("enabled"));
    }

    TEST(CommandLineDeserializerTest, HasKeyIsTheFlagBeingOnTheCommandLine) {
        ProbeReading reading({"--enabled", "--port="});
        EXPECT_TRUE(reading.Reader().HasKey("enabled"));
        EXPECT_TRUE(reading.Reader().HasKey("port"));
        EXPECT_FALSE(reading.Reader().HasKey("label"));
    }

    TEST(CommandLineDeserializerTest, AFlagIsFlatSoObjectsAndArraysFail) {
        Reading<FlagProbeNested> reading({"--top=4", "--inner", "--list=1"});
        reading.Load();
        EXPECT_EQ(reading.Value()._top, 4);
        EXPECT_EQ(Require(reading.ErrorAt("--inner")).errorMessage, "a flag holds one value");
        EXPECT_EQ(Require(reading.ErrorAt("--list")).errorMessage, "a flag holds one value");
        EXPECT_EQ(reading.Value()._inner._depth, 0);
        EXPECT_TRUE(reading.Value()._list.empty());
    }

    TEST(CommandLineDeserializerTest, AKeyLongerThanTheBufferIsNotFound) {
        const String key(kMaxFlagBytes * 4, 'k');
        const String flag = Format("--{}=1", key);
        CommandLine line;
        line.Set(Args{flag});
        CommandLineDeserializer reader(line);
        int32_t value = 5;
        EXPECT_FALSE(reader.ReadAttribute(StringView(key), value));
        EXPECT_EQ(value, 5);
        EXPECT_FALSE(reader.HasKey(StringView(key)));
        EXPECT_FALSE(reader.HasErrors());
    }

    TEST(CommandLineDeserializerTest, ValuesAreAlwaysStrict) {
        CommandLine line;
        CommandLineDeserializer reader(line);
        EXPECT_TRUE(reader.IsStrictValues());
        reader.SetStrictValues(false);
        EXPECT_TRUE(reader.IsStrictValues());
    }

    TEST(CommandLineDeserializerTest, ALookupAllocatesNothing) {
        CommandLine line;
        line.Set(Args{"--enabled", "--port=9000", "--level-count=3", "--unrelated=1"});
        CommandLineDeserializer reader(line);
        bool enabled = false;
        uint16_t port = 0;
        int32_t levels = 0;
        float scale = 0.0f;
        const auto readAll = [&] {
            reader.ReadAttribute("enabled", enabled);
            reader.ReadAttribute("port", port);
            reader.ReadAttribute("levelCount", levels);
            reader.ReadAttribute("scale", scale);
            reader.HasKey("port");
        };
        readAll();
        ExpectNoAllocations(readAll);
        EXPECT_EQ(port, 9000);
    }

    TEST(CommandLineDeserializerTest, ItOverridesAnApplicationConfigFieldByField) {
        ApplicationConfig config;
        config._control = true;
        config._controlPort = 9000;
        config._headless = true;
        config._type = "Game";

        CommandLine line;
        line.Set(Args{"--control-port=9100", "--headless=false", "--type=Other"});
        const StringView keys[] = {ApplicationConfigFields::kControl, ApplicationConfigFields::kControlPort, ApplicationConfigFields::kManualClock, ApplicationConfigFields::kHeadless};
        CommandLineDeserializer reader(line, keys);
        config.Deserialize(reader);
        reader.FinishLoad();

        EXPECT_FALSE(reader.HasErrors());
        EXPECT_TRUE(config._control);
        EXPECT_EQ(config._controlPort, 9100);
        EXPECT_FALSE(config._headless);
        EXPECT_EQ(config._type, "Game");
    }

    namespace {

        /// A probe reading with the diagnostics set, its warnings or errors as `path: message`
        class Diagnosed {
        public:
            Diagnosed(const Args& args, DiagnosticsMode mode) : _reading(args) {
                _reading.Reader().SetDiagnostics(mode);
                _reading.Load();
            }

            ProbeReading& Reading() {
                return _reading;
            }

            Vector<String> Warnings() {
                return Describe(_reading.Reader().GetWarnings());
            }

            Vector<String> Errors() {
                return Describe(_reading.Reader().GetErrors());
            }

        private:
            static Vector<String> Describe(const Vector<DeserializeError>& list) {
                Vector<String> lines;
                for (const DeserializeError& error : list) {
                    lines.push_back(Format("{}: {}", error.path, error.errorMessage));
                }
                return lines;
            }

            ProbeReading _reading;
        };

    }  // namespace

    TEST(CommandLineDeserializerTest, ANearMissOfAnAskedFlagIsAWarningWithAHint) {
        Diagnosed run({"--prot=9", "--levelcount=2"}, DiagnosticsMode::Warn);
        const Vector<String> warnings = run.Warnings();
        ASSERT_EQ(warnings.size(), 2u);
        EXPECT_EQ(warnings[0], "--prot: Unknown flag '--prot', did you mean '--port'?");
        EXPECT_EQ(warnings[1], "--levelcount: Unknown flag '--levelcount', did you mean '--level-count'?");
        EXPECT_FALSE(run.Reading().Reader().HasErrors());
    }

    TEST(CommandLineDeserializerTest, AFlagTheEngineCannotKnowIsSilent) {
        Diagnosed run({"--editor", "--game", "--seed=3", "--difficulty=2", "--strict-config", "--", "--=1", "positional"}, DiagnosticsMode::Warn);
        for (const String& warning : run.Warnings()) {
            ADD_FAILURE() << warning.c_str();
        }
        EXPECT_FALSE(run.Reading().Reader().HasErrors());
    }

    TEST(CommandLineDeserializerTest, AnAskedFlagIsNeverAnUnknownFlag) {
        Diagnosed run({"--port=9", "--enabled", "--label="}, DiagnosticsMode::Warn);
        EXPECT_TRUE(run.Warnings().empty());
    }

    TEST(CommandLineDeserializerTest, OffRecordsNothingAndReadsTheSame) {
        const Args args = {"--prot=9", "--port=12", "--level-count=x"};
        Diagnosed off(args, DiagnosticsMode::Off);
        Diagnosed warn(args, DiagnosticsMode::Warn);
        EXPECT_TRUE(off.Reading().Reader().GetWarnings().empty());
        EXPECT_EQ(warn.Warnings().size(), 1u);
        EXPECT_EQ(off.Reading().Value()._port, 12);
        EXPECT_EQ(warn.Reading().Value()._port, 12);
        EXPECT_EQ(off.Errors(), warn.Errors());
        EXPECT_EQ(off.Errors().size(), 1u);
    }

    TEST(CommandLineDeserializerTest, UnderErrorTheNearMissIsAnError) {
        Diagnosed run({"--prot=9"}, DiagnosticsMode::Error);
        EXPECT_TRUE(run.Reading().Reader().GetWarnings().empty());
        const Vector<String> errors = run.Errors();
        ASSERT_EQ(errors.size(), 1u);
        EXPECT_EQ(errors[0], "--prot: Unknown flag '--prot', did you mean '--port'?");
    }

    TEST(CommandLineDeserializerTest, OnlyTheFlagsOfTheKeyListAreAsked) {
        const StringView keys[] = {"port"};
        CommandLine line;
        line.Set(Args{"--enabeld"});
        CommandLineDeserializer reader(line, keys);
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        FlagProbe probe;
        probe.Deserialize(reader);
        reader.FinishLoad();
        EXPECT_TRUE(reader.GetWarnings().empty());
    }

    TEST(CommandLineDeserializerTest, AVeryLongArgumentIsCutInTheWarning) {
        const String longName = String("--prot") + String(ReaderLimits::kMaxErrorTextBytes * 4, 'x');
        Diagnosed run({longName}, DiagnosticsMode::Warn);
        for (const DeserializeError& warning : run.Reading().Reader().GetWarnings()) {
            EXPECT_LE(warning.path.size(), ReaderLimits::kMaxErrorTextBytes);
            EXPECT_LE(warning.errorMessage.size(), ReaderLimits::kMaxErrorTextBytes);
        }
    }

    TEST(CommandLineDeserializerTest, ManyTyposKeepTheLimitOfRecordedWarnings) {
        Args args;
        for (size_t i = 0; i < ReaderLimits::kMaxRecordedErrors * 2; ++i) {
            args.push_back("--prot=1");
        }
        Diagnosed run(args, DiagnosticsMode::Warn);
        EXPECT_LE(run.Reading().Reader().GetWarnings().size(), ReaderLimits::kMaxRecordedErrors);
    }

    TEST(CommandLineDeserializerTest, OffAllocatesNothingPerLookup) {
        CommandLine line;
        line.Set(Args{"--port=9000"});
        CommandLineDeserializer reader(line);
        uint16_t port = 0;
        const auto read = [&] { reader.ReadAttribute("port", port); };
        read();
        ExpectNoAllocations(read);
    }

}  // namespace BECore::Tests
