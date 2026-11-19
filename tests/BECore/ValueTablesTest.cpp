// Named tables (Values/ValueTables.h, ByLevel): the provider on the link scope, the copy into the program at load, the type of the result and the errors. The documents
// are those of ValueTestTypes.h (their generated code is included in ValueContextTest.cpp).

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Threading/Thread.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <BECore/Values/ValueLinkScope.h>
#include <BECore/Values/ValueTables.h>
#include <Common/Printers.h>
#include <Common/Wait.h>
#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr StringView kState = "State";
        constexpr StringView kDocumentName = "units/fireball.xml";
        constexpr StringView kContext = R"(<context caster="ValueUnitStats" cast="ValueCastInfo"/>)";
        constexpr StringView kTable = "FireballDamage";
        constexpr int kLoadingThreads = 2;

        constexpr int32_t kInt32Min = std::numeric_limits<int32_t>::min();
        constexpr int32_t kInt32Max = std::numeric_limits<int32_t>::max();

        bool HasText(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        ValueTableSet FireballTables() {
            ValueTableSet tables;
            tables.Add(kTable, {10.0, 20.0, 30.0, 40.0});
            return tables;
        }

        String ReportsOf(const XmlDeserializer& reader) {
            String out;
            for (const Vector<DeserializeError>* list : {&reader.GetErrors(), &reader.GetWarnings()}) {
                for (const DeserializeError& report : *list) {
                    AppendFormat(out, "{}: {}\n", report.path, report.errorMessage);
                }
            }
            return out;
        }

        /// Loads `<root><State>` the context and @p fields `</State></root>` into @p document under the scope of the caller; every report, one text
        String LoadDocument(StringView fields, ValueFormulaDocument& document) {
            XmlDeserializer reader;
            const String xml = Format("<root><State>{}{}</State></root>", kContext, fields);
            EXPECT_TRUE(reader.LoadFromString(xml)) << xml.c_str();
            EXPECT_TRUE(reader.BeginObject(kState));
            document.Deserialize(reader);
            reader.EndObject();
            return ReportsOf(reader);
        }

        /// A formula of a field the document names, loaded under a scope with @p tables
        template <typename T>
        std::expected<Value<T>, ProgramError> CompileUnder(const ValueWorld& world, const IValueTables* tables, StringView text) {
            CompileOptions options;
            options.tables = tables;
            return CompileValue<T>(text, world.declaration, options);
        }

        /// The formula must compile under @p tables
        template <typename T>
        Value<T> Compiled(const ValueWorld& world, const IValueTables& tables, StringView text) {
            const auto value = CompileUnder<T>(world, &tables, text);
            EXPECT_TRUE(value.has_value()) << String(text).c_str() << ": " << (value.has_value() ? String() : value.error().message).c_str();
            return value.value_or(Value<T>{});
        }

    }  // namespace

    TEST(ValueTablesTest, AFormulaLoadedFromXmlReadsTheTableOfTheScopeByAZeroBasedLevel) {
        const ValueTableSet tables = FireballTables();
        ValueLinkScope scope(kDocumentName);
        scope.UseTables(tables);
        ValueFormulaDocument document;
        const String reports = LoadDocument(R"x(<damage expr="ByLevel('FireballDamage', cast.level) * 2"/>)x", document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ASSERT_EQ(document.damage.Kind(), ValueKind::Program);

        ValueWorld world;
        struct Row {
            int32_t level;
            int32_t expected;
        };
        // Entry 0 is level 0; below the first entry and past the last are the ends
        const Row rows[] = {{kInt32Min, 20}, {-5, 20}, {-1, 20}, {0, 20}, {1, 40}, {2, 60}, {3, 80}, {4, 80}, {100, 80}, {kInt32Max, 80}};
        for (const Row& row : rows) {
            world.cast.level = row.level;
            EXPECT_EQ(document.damage.Evaluate(world.context), row.expected) << "level " << row.level;
        }
    }

    TEST(ValueTablesTest, TheProgramKeepsNothingOfTheProvider) {
        ValueWorld world;
        Value<int32_t> value;
        {
            ValueTableSet tables = FireballTables();
            value = Compiled<int32_t>(world, tables, "ByLevel('FireballDamage', cast.level)");
            // Changing the table after the load changes nothing either
            tables.Add(kTable, {1.0});
            world.cast.level = 2;
            EXPECT_EQ(value.Evaluate(world.context), 30);
        }
        for (int32_t level = -1; level <= 4; ++level) {
            world.cast.level = level;
            EXPECT_EQ(value.Evaluate(world.context), 10 * (std::clamp(level, 0, 3) + 1)) << level;
        }
    }

    TEST(ValueTablesTest, AnIntFieldWithAFractionInTheTableIsALoadErrorThatNamesTheTableAndTheEntry) {
        ValueTableSet tables;
        tables.Add("Crit", {10.0, 12.5, 14.0});
        ValueLinkScope scope(kDocumentName);
        scope.UseTables(tables);
        ValueFormulaDocument document;
        const String reports = LoadDocument(R"x(<damage expr="ByLevel('Crit', cast.level)"/>)x", document);
        EXPECT_TRUE(HasText(reports, "damage")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "table 'Crit'")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "entry 1")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "12.5")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, kDocumentName)) << reports.c_str();
        EXPECT_EQ(document.damage.Kind(), ValueKind::Const);
        EXPECT_EQ(document.damage.GetConst(), 7) << "a failed load leaves the field as it was";

        // The same table is fine for a float and a double field
        ValueLinkScope floatScope(kDocumentName);
        floatScope.UseTables(tables);
        ValueFormulaDocument floats;
        EXPECT_TRUE(LoadDocument(R"x(<power expr="ByLevel('Crit', cast.level)"/><precise expr="ByLevel('Crit', 1) + 0.25"/>)x", floats).empty());
        ValueWorld world;
        world.cast.level = 1;
        EXPECT_EQ(floats.power.Evaluate(world.context), 12.5f);
        EXPECT_EQ(floats.precise.Evaluate(world.context), 12.75);
    }

    TEST(ValueTablesTest, TheEntriesOfAnIntegerTypeMustBeInItsRange) {
        ValueTableSet tables;
        tables.Add("Big", {1.0, 3.0e9});
        tables.Add("Huge", {1.0, 1.0e19});
        tables.Add("Nan", {std::numeric_limits<double>::quiet_NaN()});
        tables.Add("Wide", {1.0, 3.0e9, -9.0e18});
        ValueWorld world;
        const auto big = CompileUnder<int32_t>(world, &tables, "ByLevel('Big', cast.level)");
        ASSERT_FALSE(big.has_value());
        EXPECT_TRUE(HasText(big.error().message, "table 'Big'")) << big.error().message.c_str();
        EXPECT_TRUE(HasText(big.error().message, "entry 1")) << big.error().message.c_str();
        EXPECT_TRUE(HasText(big.error().message, "range")) << big.error().message.c_str();
        EXPECT_FALSE(CompileUnder<int64_t>(world, &tables, "ByLevel('Huge', cast.level)").has_value());
        EXPECT_FALSE(CompileUnder<int32_t>(world, &tables, "ByLevel('Nan', cast.level)").has_value());
        const auto wide = CompileUnder<int64_t>(world, &tables, "ByLevel('Wide', cast.level)");
        ASSERT_TRUE(wide.has_value());
        world.cast.level = 2;
        EXPECT_EQ(wide->Evaluate(world.context), -9000000000000000000LL);
        world.cast.level = 1;
        EXPECT_EQ(wide->Evaluate(world.context), 3000000000LL);
    }

    TEST(ValueTablesTest, TheResultHasTheTypeBesideItAndDoubleWhenNothingIsBesideIt) {
        const ValueTableSet tables = FireballTables();
        ValueWorld world;
        world.cast.level = 1;
        world.caster.attack = 5;
        EXPECT_EQ(Compiled<int64_t>(world, tables, "ByLevel('FireballDamage', cast.level) + caster.score").Evaluate(world.context), 20 + 40000000000);
        EXPECT_EQ(Compiled<float>(world, tables, "ByLevel('FireballDamage', cast.level) * caster.speed").Evaluate(world.context), 30.0f);
        EXPECT_EQ(Compiled<double>(world, tables, "ByLevel('FireballDamage', cast.level) / 8").Evaluate(world.context), 2.5);
        // The index of the table reads the context, yet the operand that has a type of its own decides
        EXPECT_EQ(Compiled<int32_t>(world, tables, "ByLevel('FireballDamage', cast.level) + caster.attack").Evaluate(world.context), 25);
        EXPECT_TRUE(Compiled<bool>(world, tables, "ByLevel('FireballDamage', cast.level) > caster.attack").Evaluate(world.context));
        EXPECT_TRUE(Compiled<bool>(world, tables, "ByLevel('FireballDamage', cast.level) > 19.5").Evaluate(world.context));
        EXPECT_EQ(Compiled<int32_t>(world, tables, "max(caster.attack, ByLevel('FireballDamage', cast.level))").Evaluate(world.context), 20);
        // A constant index folds
        const Value<int32_t> folded = Compiled<int32_t>(world, tables, "ByLevel('FireballDamage', 2) + 1");
        ASSERT_TRUE(folded.IsConst());
        EXPECT_EQ(folded.GetConst(), 31);
    }

    TEST(ValueTablesTest, ATypeThatIsNoNumberIsRefusedAndPointsToAt) {
        const ValueTableSet tables = FireballTables();
        ValueWorld world;
        const auto vector = CompileUnder<Vec3>(world, &tables, "ByLevel('FireballDamage', cast.level)");
        ASSERT_FALSE(vector.has_value());
        EXPECT_EQ(vector.error().column, 1U);
        EXPECT_TRUE(HasText(vector.error().message, "a named table holds numbers")) << vector.error().message.c_str();
        EXPECT_TRUE(HasText(vector.error().message, "at(i, ...)")) << vector.error().message.c_str();
        EXPECT_FALSE(CompileUnder<bool>(world, &tables, "ByLevel('FireballDamage', cast.level)").has_value());
        const auto element = CompileUnder<ValueElement>(world, &tables, "ByLevel('FireballDamage', cast.level)");
        ASSERT_FALSE(element.has_value());
        EXPECT_TRUE(HasText(element.error().message, "a named table holds numbers")) << element.error().message.c_str();
        EXPECT_FALSE(CompileUnder<PoolString>(world, &tables, "ByLevel('FireballDamage', cast.level)").has_value());
    }

    TEST(ValueTablesTest, NoProviderAnUnknownNameAnEmptyTableAndATableOverTheLimitAreErrors) {
        ValueWorld world;
        const auto none = CompileUnder<int32_t>(world, nullptr, "ByLevel('FireballDamage', cast.level)");
        ASSERT_FALSE(none.has_value());
        EXPECT_TRUE(HasText(none.error().message, "no tables are declared where this formula is read")) << none.error().message.c_str();
        EXPECT_TRUE(HasText(none.error().message, "FireballDamage")) << none.error().message.c_str();

        ValueTableSet tables = FireballTables();
        tables.Add("Empty", {});
        tables.Add("Frost", {1.0});
        tables.Add("Long", Vector<double>(kMaxTableEntries + 1, 1.0));
        tables.Add("Max", Vector<double>(kMaxTableEntries, 1.0));

        const String typo = "ByLevel('FireballDamge', cast.level)";
        const auto unknown = CompileUnder<int32_t>(world, &tables, typo);
        ASSERT_FALSE(unknown.has_value());
        EXPECT_EQ(unknown.error().column, 9U);
        EXPECT_TRUE(HasText(unknown.error().message, "no table 'FireballDamge'")) << unknown.error().message.c_str();
        EXPECT_TRUE(HasText(unknown.error().message, "FireballDamage")) << unknown.error().message.c_str();
        EXPECT_TRUE(HasText(unknown.error().message, "Frost")) << unknown.error().message.c_str();

        const auto empty = CompileUnder<int32_t>(world, &tables, "ByLevel('Empty', cast.level)");
        ASSERT_FALSE(empty.has_value());
        EXPECT_TRUE(HasText(empty.error().message, "table 'Empty' is empty")) << empty.error().message.c_str();
        const auto over = CompileUnder<int32_t>(world, &tables, "ByLevel('Long', cast.level)");
        ASSERT_FALSE(over.has_value());
        EXPECT_TRUE(HasText(over.error().message, "table 'Long'")) << over.error().message.c_str();
        EXPECT_TRUE(HasText(over.error().message, Format("at most {}", kMaxTableEntries))) << over.error().message.c_str();
        EXPECT_TRUE(CompileUnder<int32_t>(world, &tables, "ByLevel('Max', cast.level)").has_value());
    }

    TEST(ValueTablesTest, TheArgumentsOfByLevelAreANameLiteralAndAnInt) {
        const ValueTableSet tables = FireballTables();
        ValueWorld world;
        const auto expect = [&](StringView text, uint32_t column, StringView words) {
            const auto value = CompileUnder<int32_t>(world, &tables, text);
            ASSERT_FALSE(value.has_value()) << String(text).c_str();
            EXPECT_EQ(value.error().column, column) << String(text).c_str() << ": " << value.error().message.c_str();
            EXPECT_TRUE(HasText(value.error().message, words)) << String(text).c_str() << ": " << value.error().message.c_str();
        };
        expect("ByLevel(cast.level)", 1, "takes the name of a table and an index");
        expect("ByLevel('FireballDamage')", 1, "takes the name of a table and an index");
        expect("ByLevel(3, cast.level)", 9, "name of the table as a 'text' literal");
        expect("ByLevel('FireballDamage', cast.power)", 27, "an int as its index");
        expect("ByLevel('FireballDamage', caster.score)", 27, "int(x)");
        expect("ByLevel", 1, "'ByLevel' is a function");
    }

    TEST(ValueTablesTest, TheInnermostScopeThatHasTablesProvidesThem) {
        EXPECT_EQ(ValueLinkScope::Tables(), nullptr);
        const ValueTableSet outer = FireballTables();
        ValueTableSet other;
        other.Add(kTable, {1.0, 2.0});
        {
            ValueLinkScope outerScope(kDocumentName);
            outerScope.UseTables(outer);
            EXPECT_EQ(ValueLinkScope::Tables(), &outer);
            {
                // A scope without tables does not hide the one around it
                const ValueLinkScope inner("inner.xml");
                EXPECT_EQ(ValueLinkScope::Tables(), &outer);
                ValueFormulaDocument document;
                EXPECT_TRUE(LoadDocument(R"x(<damage expr="ByLevel('FireballDamage', 3)"/>)x", document).empty());
                EXPECT_EQ(document.damage.GetConst(), 40);
            }
            {
                ValueLinkScope inner("inner.xml");
                inner.UseTables(other);
                EXPECT_EQ(ValueLinkScope::Tables(), &other);
                ValueFormulaDocument document;
                EXPECT_TRUE(LoadDocument(R"x(<damage expr="ByLevel('FireballDamage', 3)"/>)x", document).empty());
                EXPECT_EQ(document.damage.GetConst(), 2);
            }
            EXPECT_EQ(ValueLinkScope::Tables(), &outer);
        }
        EXPECT_EQ(ValueLinkScope::Tables(), nullptr);
    }

    TEST(ValueTablesTest, ADocumentLoadedWithoutTablesNamesTheDocumentInTheError) {
        const ValueLinkScope scope(kDocumentName);
        ValueFormulaDocument document;
        const String reports = LoadDocument(R"x(<damage expr="ByLevel('FireballDamage', cast.level)"/>)x", document);
        EXPECT_TRUE(HasText(reports, "no tables are declared where this formula is read")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, kDocumentName)) << reports.c_str();
    }

    TEST(ValueTablesTest, TwoThreadsEachSeeTheirOwnProvider) {
        ValueTableSet first;
        first.Add(kTable, {1.0, 2.0, 3.0});
        ValueTableSet second;
        second.Add(kTable, {100.0, 200.0, 300.0});

        struct Outcome {
            bool metOther = false;
            bool sawOwn = true;
        };
        std::atomic<int> arrived{0};
        const auto work = [&arrived](const ValueTableSet& tables, int32_t scale, Outcome& outcome) {
            arrived.fetch_add(1);
            outcome.metOther = WaitUntil([&arrived] { return arrived.load() == kLoadingThreads; });
            for (int round = 0; round < 50 && outcome.sawOwn; ++round) {
                // A scope takes one `<context>`, so each load has its own
                ValueLinkScope scope("thread.xml");
                scope.UseTables(tables);
                ValueFormulaDocument document;
                const String reports = LoadDocument(R"x(<damage expr="ByLevel('FireballDamage', 2)"/>)x", document);
                outcome.sawOwn = reports.empty() && document.damage.IsConst() && document.damage.GetConst() == 3 * scale && ValueLinkScope::Tables() == &tables;
            }
        };
        Outcome a;
        Outcome b;
        {
            Thread threadA("ValueTables A", [&] { work(first, 1, a); });
            Thread threadB("ValueTables B", [&] { work(second, 100, b); });
            threadA.join();
            threadB.join();
        }
        EXPECT_TRUE(a.metOther);
        EXPECT_TRUE(b.metOther);
        EXPECT_TRUE(a.sawOwn);
        EXPECT_TRUE(b.sawOwn);
        EXPECT_EQ(ValueLinkScope::Tables(), nullptr);
    }

    TEST(ValueTablesTest, TheTableSetListsItsNamesInOrderAndReplacesATableOfTheSameName) {
        ValueTableSet tables;
        tables.Add("Water", {1.0});
        tables.Add("Fire", {2.0, 3.0});
        tables.Add("Water", {4.0, 5.0, 6.0});
        Vector<String> names;
        tables.Names(names);
        ASSERT_EQ(names.size(), 2U);
        EXPECT_EQ(names[0], "Fire");
        EXPECT_EQ(names[1], "Water");
        ASSERT_TRUE(tables.Find("Water").has_value());
        EXPECT_EQ(tables.Find("Water")->size(), 3U);
        EXPECT_FALSE(tables.Find("Earth").has_value());
    }

}  // namespace BECore::Tests
