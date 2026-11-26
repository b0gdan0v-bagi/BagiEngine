#include <BECore/PanelBindingProbe.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLimits.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelBindingWriter.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Common/AllocCheck.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Generated/PanelBindingProbe.gen.hpp>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <tuple>

namespace BECore::Tests {

    namespace {

        PanelBinding MustParse(StringView text) {
            const auto parsed = ParseBinding(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str();
            return parsed.value_or(PanelBinding{});
        }

    }  // namespace

    TEST(PanelBindingTest, ParseSplitsTheSourceFromThePath) {
        const PanelBinding plain = MustParse("files.pending");
        EXPECT_EQ(plain.source, "files");
        EXPECT_EQ(plain.path, "pending");
        EXPECT_FALSE(plain.negated);

        const PanelBinding indexed = MustParse("a.b[2].c");
        EXPECT_EQ(indexed.source, "a");
        EXPECT_EQ(indexed.path, "b[2].c");
    }

    TEST(PanelBindingTest, AQuotedKeyStaysInThePath) {
        const auto parsed = ParseBinding("m.table[\"x y\"].v");
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed->source, "m");
        EXPECT_EQ(parsed->path, "table[\"x y\"].v");
    }

    TEST(PanelBindingTest, ParseRefusesMalformedText) {
        for (const StringView bad : {"", "files", ".path", "files.", "1x.y", "a b.c", "a.b[", "a.[1]x", "!a.b"}) {
            EXPECT_FALSE(ParseBinding(bad).has_value()) << String(bad).c_str();
        }
    }

    TEST(PanelBindingTest, ParseObjectBindingTakesASourceAlone) {
        const auto whole = ParseObjectBinding("game");
        ASSERT_TRUE(whole.has_value());
        EXPECT_EQ(whole->source, "game");
        EXPECT_TRUE(whole->path.empty());
        EXPECT_FALSE(whole->negated);

        const auto inner = ParseObjectBinding("game.fields");
        ASSERT_TRUE(inner.has_value());
        EXPECT_EQ(inner->source, "game");
        EXPECT_EQ(inner->path, "fields");

        for (const StringView bad : {"", "!game", "1x", "a b", "game.", "game.a[", ".game"}) {
            EXPECT_FALSE(ParseObjectBinding(bad).has_value()) << String(bad).c_str();
        }
        const String tooLong(PanelAssetLimits::kMaxTextBytes + 1, 'a');
        EXPECT_FALSE(ParseObjectBinding(tooLong).has_value());
    }

    TEST(PanelBindingTest, ParseConditionTakesALeadingNegation) {
        const auto negated = ParseCondition("!files.running");
        ASSERT_TRUE(negated.has_value());
        EXPECT_TRUE(negated->negated);
        EXPECT_EQ(negated->source, "files");
        EXPECT_EQ(negated->path, "running");

        const auto plain = ParseCondition("files.running");
        ASSERT_TRUE(plain.has_value());
        EXPECT_FALSE(plain->negated);

        EXPECT_FALSE(ParseCondition("!!a.b").has_value());
        EXPECT_FALSE(ParseCondition("!").has_value());
    }

    TEST(PanelBindingTest, ParseErrorIsOneShortLine) {
        const auto parsed = ParseBinding("files");
        ASSERT_FALSE(parsed.has_value());
        EXPECT_FALSE(parsed.error().empty());
        EXPECT_EQ(parsed.error().find('\n'), String::npos);
    }

    TEST(PanelDataSourcesTest, FindsARegisteredObjectByName) {
        PanelDataSources sources;
        PanelProbe probe;
        probe.count = 7;
        const PanelSourceHandle handle = sources.Register("probe", [&probe] { return TypedRef::Of(probe); });

        const TypedRef found = sources.Find("probe");
        ASSERT_TRUE(found);
        EXPECT_EQ(found.object, &probe);
        EXPECT_TRUE(sources.Contains("probe"));
        EXPECT_FALSE(sources.Find("other"));
        EXPECT_FALSE(sources.Contains("other"));
    }

    TEST(PanelDataSourcesTest, TheProviderIsAskedAtEveryRead) {
        PanelDataSources sources;
        PanelProbe first;
        PanelProbe second;
        PanelProbe* current = &first;
        const PanelSourceHandle handle = sources.Register("game", [&current] { return current != nullptr ? TypedRef::Of(*current) : TypedRef{}; });

        EXPECT_EQ(sources.Find("game").object, &first);
        current = &second;
        EXPECT_EQ(sources.Find("game").object, &second);
        current = nullptr;
        EXPECT_FALSE(sources.Find("game"));
        EXPECT_TRUE(sources.Contains("game"));
    }

    TEST(PanelDataSourcesTest, TheHandleUnregistersOnDestruction) {
        PanelDataSources sources;
        PanelProbe probe;
        {
            const PanelSourceHandle handle = sources.Register("probe", [&probe] { return TypedRef::Of(probe); });
            EXPECT_TRUE(sources.Find("probe"));
        }
        EXPECT_FALSE(sources.Contains("probe"));
    }

    TEST(PanelDataSourcesTest, AMovedHandleKeepsTheRegistration) {
        PanelDataSources sources;
        PanelProbe probe;
        PanelSourceHandle outer;
        {
            PanelSourceHandle inner = sources.Register("probe", [&probe] { return TypedRef::Of(probe); });
            outer = std::move(inner);
            EXPECT_FALSE(inner);
        }
        EXPECT_TRUE(sources.Find("probe"));
        outer.Reset();
        EXPECT_FALSE(sources.Contains("probe"));
    }

    TEST(PanelDataSourcesTest, ARegistrationReplacesTheOldOneAndTheOldHandleRemovesNothing) {
        PanelDataSources sources;
        PanelProbe oldProbe;
        PanelProbe newProbe;
        PanelSourceHandle oldHandle = sources.Register("probe", [&oldProbe] { return TypedRef::Of(oldProbe); });
        PanelSourceHandle newHandle = sources.Register("probe", [&newProbe] { return TypedRef::Of(newProbe); });

        EXPECT_EQ(sources.Find("probe").object, &newProbe);
        oldHandle.Reset();
        EXPECT_EQ(sources.Find("probe").object, &newProbe);
        newHandle.Reset();
        EXPECT_FALSE(sources.Contains("probe"));
    }

    TEST(PanelDataSourcesTest, AnOwnSourceWinsOverTheParentAndTheParentIsFoundBehindIt) {
        PanelDataSources parent;
        PanelDataSources child;
        PanelProbe parentProbe;
        PanelProbe childProbe;
        PanelProbe onlyInParent;
        const PanelSourceHandle parentShared = parent.Register("shared", [&parentProbe] { return TypedRef::Of(parentProbe); });
        const PanelSourceHandle parentOnly = parent.Register("only", [&onlyInParent] { return TypedRef::Of(onlyInParent); });
        const PanelSourceHandle childShared = child.Register("shared", [&childProbe] { return TypedRef::Of(childProbe); });
        child.SetParent(&parent);

        EXPECT_EQ(child.Find("shared").object, &childProbe);
        EXPECT_EQ(child.Find("only").object, &onlyInParent);
        EXPECT_TRUE(child.Contains("only"));
        EXPECT_FALSE(child.Contains("nowhere"));
        EXPECT_FALSE(parent.Find("nowhere"));
    }

    TEST(PanelDataSourcesTest, AParentChainIsFollowedAndAnEmptyOwnSourceIsNotSkipped) {
        PanelDataSources grandparent;
        PanelDataSources parent;
        PanelDataSources child;
        PanelProbe probe;
        bool gone = false;
        const PanelSourceHandle far = grandparent.Register("far", [&probe, &gone] { return gone ? TypedRef{} : TypedRef::Of(probe); });
        const PanelSourceHandle shadow = child.Register("far", [] { return TypedRef{}; });
        const PanelSourceHandle near = grandparent.Register("near", [&probe, &gone] { return gone ? TypedRef{} : TypedRef::Of(probe); });
        parent.SetParent(&grandparent);
        child.SetParent(&parent);

        EXPECT_EQ(child.Find("near").object, &probe);
        EXPECT_FALSE(child.Find("far")) << "a name the own sources hold is not looked up in the parent";
        gone = true;
        EXPECT_FALSE(child.Find("near"));
        EXPECT_TRUE(child.Contains("near"));
    }

    TEST(PanelDataSourcesTest, ARegistryRefusesToBeItsOwnParent) {
        PanelDataSources sources;
        sources.SetParent(&sources);
        EXPECT_FALSE(sources.Find("anything"));
        EXPECT_FALSE(sources.Contains("anything"));
    }

    TEST(PanelDataSourcesTest, ALookupNeverInternsTheName) {
        PanelDataSources sources;
        const String fresh = FreshText("panel_source");
        EXPECT_FALSE(sources.Find(fresh));
        EXPECT_FALSE(PoolString::Find(StringView(fresh)).has_value());
    }

    namespace {

        constexpr StringView kSource = "p";
        constexpr size_t kManyRows = 100;
        constexpr int32_t kProbeCount = 7;
        constexpr int32_t kRowSize = 40;

        PanelBinding Bind(StringView path, bool negated = false) {
            PanelBinding binding;
            binding.source = String(kSource);
            binding.path = String(path);
            binding.negated = negated;
            return binding;
        }

        PanelProbeRow MakeRow(StringView path, int32_t size, bool done = false) {
            PanelProbeRow row;
            row.path = String(path);
            row.size = size;
            row.done = done;
            return row;
        }

        class PanelBindingReadTest : public ::testing::Test {
        protected:
            void SetUp() override {
                probe.count = kProbeCount;
                probe.title = "Files";
                probe.tag = "tag-text"_intern;
                probe.code = "fixed";
                probe.mode = PanelProbeMode::Busy;
                probe.inner.path = "inner/path";
                probe.rows = {MakeRow("a.txt", 10, true), MakeRow("b.txt", kRowSize)};
                probe.rows[1].mode = PanelProbeMode::Failed;
                probe.numbers = {5, 6, 7};
                probe.names = {"x", "y"};
                handle = sources.Register(kSource, [this] { return current != nullptr ? TypedRef::Of(*current) : TypedRef{}; });
            }

            String Text(StringView path) {
                StringView out;
                EXPECT_EQ(reader.ReadText(sources, Bind(path), scratch, out), PanelReadStatus::Ok) << String(path).c_str();
                return String(out);
            }

            PanelDataSources sources;
            PanelBindingReader reader;
            PanelProbe probe;
            PanelProbe* current = &probe;
            PanelSourceHandle handle;
            String scratch;
        };

    }  // namespace

    TEST_F(PanelBindingReadTest, ReadsABoolAndInvertsItForANegatedCondition) {
        bool value = false;
        probe.flag = true;
        EXPECT_EQ(reader.ReadBool(sources, Bind("flag"), value), PanelReadStatus::Ok);
        EXPECT_TRUE(value);
        EXPECT_EQ(reader.ReadBool(sources, Bind("flag", true), value), PanelReadStatus::Ok);
        EXPECT_FALSE(value);
    }

    TEST_F(PanelBindingReadTest, ReadsEveryNumberKindAsADouble) {
        probe.small = 200;
        probe.ratio = 0.5f;
        probe.wide = -1.25;
        double value = 0.0;
        EXPECT_EQ(reader.ReadNumber(sources, Bind("count"), value), PanelReadStatus::Ok);
        EXPECT_EQ(value, kProbeCount);
        EXPECT_EQ(reader.ReadNumber(sources, Bind("small"), value), PanelReadStatus::Ok);
        EXPECT_EQ(value, 200.0);
        EXPECT_EQ(reader.ReadNumber(sources, Bind("ratio"), value), PanelReadStatus::Ok);
        EXPECT_EQ(value, 0.5);
        EXPECT_EQ(reader.ReadNumber(sources, Bind("wide"), value), PanelReadStatus::Ok);
        EXPECT_EQ(value, -1.25);
    }

    TEST_F(PanelBindingReadTest, TellsAnIntegerFieldFromAFloatOneAlsoWhenItIsReachedThroughTheLiveObject) {
        bool isInteger = false;
        double value = 0.0;
        EXPECT_EQ(reader.ReadNumber(sources, Bind("small"), value, &isInteger), PanelReadStatus::Ok);
        EXPECT_TRUE(isInteger);
        EXPECT_EQ(reader.ReadNumber(sources, Bind("ratio"), value, &isInteger), PanelReadStatus::Ok);
        EXPECT_FALSE(isInteger);
        EXPECT_EQ(reader.ReadNumber(sources, Bind("wide"), value, &isInteger), PanelReadStatus::Ok);
        EXPECT_FALSE(isInteger);
        probe.numbers = {1, 2, 3};
        EXPECT_EQ(reader.ReadNumber(sources, Bind("numbers[1]"), value, &isInteger), PanelReadStatus::Ok);
        EXPECT_TRUE(isInteger);
    }

    TEST_F(PanelBindingReadTest, ReadsATextOfEveryKind) {
        probe.flag = true;
        probe.ratio = 0.1f;
        EXPECT_EQ(Text("title"), "Files");
        EXPECT_EQ(Text("tag"), "tag-text");
        EXPECT_EQ(Text("code"), "fixed");
        EXPECT_EQ(Text("mode"), "Busy");
        EXPECT_EQ(Text("flag"), "true");
        EXPECT_EQ(Text("count"), "7");
        EXPECT_EQ(Text("ratio"), "0.1");
        EXPECT_EQ(Text("inner.path"), "inner/path");
    }

    TEST_F(PanelBindingReadTest, ReadsThroughAnOptionalAndAnElementOfAList) {
        StringView out;
        EXPECT_EQ(reader.ReadText(sources, Bind("maybe"), scratch, out), PanelReadStatus::NoValue);
        probe.maybe = 12;
        EXPECT_EQ(Text("maybe"), "12");
        EXPECT_EQ(Text("numbers[1]"), "6");
        double number = 0.0;
        EXPECT_EQ(reader.ReadNumber(sources, Bind("numbers[2]"), number), PanelReadStatus::Ok);
        EXPECT_EQ(number, 7.0);
        EXPECT_EQ(reader.ReadNumber(sources, Bind("numbers[9]"), number), PanelReadStatus::NoValue);
        EXPECT_EQ(Text("rows[1].path"), "b.txt");
    }

    TEST_F(PanelBindingReadTest, AWrongPathTypeOrSourceIsAStatus) {
        bool flag = false;
        double number = 0.0;
        StringView text;
        EXPECT_EQ(reader.ReadBool(sources, Bind("count"), flag), PanelReadStatus::WrongType);
        EXPECT_EQ(reader.ReadNumber(sources, Bind("title"), number), PanelReadStatus::WrongType);
        EXPECT_EQ(reader.ReadNumber(sources, Bind("mode"), number), PanelReadStatus::WrongType);
        EXPECT_EQ(reader.ReadText(sources, Bind("rows"), scratch, text), PanelReadStatus::WrongType);
        EXPECT_EQ(reader.ReadText(sources, Bind("inner"), scratch, text), PanelReadStatus::WrongType);
        EXPECT_EQ(reader.ReadBool(sources, Bind("nope"), flag), PanelReadStatus::BadPath);
        EXPECT_EQ(reader.ReadBool(sources, Bind("flag.deeper"), flag), PanelReadStatus::BadPath);

        PanelBinding other = Bind("flag");
        other.source = "elsewhere";
        EXPECT_EQ(reader.ReadBool(sources, other, flag), PanelReadStatus::UnknownSource);

        current = nullptr;
        EXPECT_EQ(reader.ReadBool(sources, Bind("flag"), flag), PanelReadStatus::NoObject);
        size_t count = 0;
        EXPECT_EQ(reader.ListCount(sources, Bind("rows"), count), PanelReadStatus::NoObject);
    }

    TEST_F(PanelBindingReadTest, ACompiledPathIsKeptPerTypeOfTheObject) {
        PanelProbeOther other;
        other.count = 2.5f;
        PanelProbeOther* otherPtr = &other;
        const PanelSourceHandle second = sources.Register("q", [&otherPtr] { return TypedRef::Of(*otherPtr); });
        PanelBinding same = Bind("count");
        same.source = "q";

        double value = 0.0;
        EXPECT_EQ(reader.ReadNumber(sources, Bind("count"), value), PanelReadStatus::Ok);
        EXPECT_EQ(value, kProbeCount);
        EXPECT_EQ(reader.ReadNumber(sources, same, value), PanelReadStatus::Ok);
        EXPECT_EQ(value, 2.5);
        EXPECT_EQ(reader.ReadNumber(sources, Bind("count"), value), PanelReadStatus::Ok);
        EXPECT_EQ(value, kProbeCount);
    }

    TEST_F(PanelBindingReadTest, APathThatDoesNotCompileIsRememberedToo) {
        bool flag = false;
        EXPECT_EQ(reader.ReadBool(sources, Bind("nope"), flag), PanelReadStatus::BadPath);
        const size_t held = reader.CompiledCount();
        EXPECT_EQ(reader.ReadBool(sources, Bind("nope"), flag), PanelReadStatus::BadPath);
        EXPECT_EQ(reader.CompiledCount(), held);
    }

    TEST_F(PanelBindingReadTest, TheCacheIsBounded) {
        bool flag = false;
        for (size_t i = 0; i < PanelBindingReader::kMaxCompiledPaths + 10; ++i) {
            EXPECT_EQ(reader.ReadBool(sources, Bind(Format("missing{}", i)), flag), PanelReadStatus::BadPath);
        }
        EXPECT_LE(reader.CompiledCount(), PanelBindingReader::kMaxCompiledPaths);
    }

    TEST_F(PanelBindingReadTest, CountsTheRowsOfAList) {
        size_t count = 99;
        EXPECT_EQ(reader.ListCount(sources, Bind("rows"), count), PanelReadStatus::Ok);
        EXPECT_EQ(count, 2u);
        EXPECT_EQ(reader.ListCount(sources, Bind("numbers"), count), PanelReadStatus::Ok);
        EXPECT_EQ(count, 3u);
        probe.rows.clear();
        EXPECT_EQ(reader.ListCount(sources, Bind("rows"), count), PanelReadStatus::Ok);
        EXPECT_EQ(count, 0u);
        EXPECT_EQ(reader.ListCount(sources, Bind("count"), count), PanelReadStatus::NotAList);
        EXPECT_EQ(reader.ListCount(sources, Bind("nope"), count), PanelReadStatus::BadPath);
        EXPECT_EQ(reader.ListCount(sources, Bind("maybe"), count), PanelReadStatus::NotAList);
    }

    TEST_F(PanelBindingReadTest, ReadsACellOfARow) {
        StringView text;
        bool done = false;
        EXPECT_EQ(reader.ReadRowText(sources, Bind("rows"), 1, "path", scratch, text), PanelReadStatus::Ok);
        EXPECT_EQ(text, "b.txt");
        EXPECT_EQ(reader.ReadRowText(sources, Bind("rows"), 1, "size", scratch, text), PanelReadStatus::Ok);
        EXPECT_EQ(text, "40");
        EXPECT_EQ(reader.ReadRowText(sources, Bind("rows"), 1, "mode", scratch, text), PanelReadStatus::Ok);
        EXPECT_EQ(text, "Failed");
        EXPECT_EQ(reader.ReadRowBool(sources, Bind("rows"), 0, "done", done), PanelReadStatus::Ok);
        EXPECT_TRUE(done);
        EXPECT_EQ(reader.ReadRowBool(sources, Bind("rows"), 1, "done", done), PanelReadStatus::Ok);
        EXPECT_FALSE(done);
    }

    TEST_F(PanelBindingReadTest, ReadsTheRowItselfOfAListOfScalars) {
        StringView text;
        EXPECT_EQ(reader.ReadRowText(sources, Bind("numbers"), 2, "", scratch, text), PanelReadStatus::Ok);
        EXPECT_EQ(text, "7");
        EXPECT_EQ(reader.ReadRowText(sources, Bind("names"), 1, "", scratch, text), PanelReadStatus::Ok);
        EXPECT_EQ(text, "y");
        EXPECT_EQ(reader.ReadRowText(sources, Bind("numbers"), 0, "field", scratch, text), PanelReadStatus::WrongType);
    }

    TEST_F(PanelBindingReadTest, ARowOrACellThatIsNotThereIsAStatus) {
        StringView text;
        EXPECT_EQ(reader.ReadRowText(sources, Bind("rows"), 2, "path", scratch, text), PanelReadStatus::NoRow);
        EXPECT_EQ(reader.ReadRowText(sources, Bind("rows"), 0, "nope", scratch, text), PanelReadStatus::BadPath);
        EXPECT_EQ(reader.ReadRowText(sources, Bind("count"), 0, "path", scratch, text), PanelReadStatus::NotAList);
        probe.rows.clear();
        EXPECT_EQ(reader.ReadRowText(sources, Bind("rows"), 0, "path", scratch, text), PanelReadStatus::NoRow);
    }

    TEST_F(PanelBindingReadTest, AFixedArrayIsAList) {
        // The elements of an Array sit inside the field: the other layout of the list descriptor
        probe.triple = {3, 4, 5};
        size_t count = 0;
        StringView text;
        EXPECT_EQ(reader.ListCount(sources, Bind("triple"), count), PanelReadStatus::Ok);
        EXPECT_EQ(count, 3u);
        EXPECT_EQ(reader.ReadRowText(sources, Bind("triple"), 2, "", scratch, text), PanelReadStatus::Ok);
        EXPECT_EQ(text, "5");
    }

    TEST_F(PanelBindingReadTest, ReadsTheIndexAndTheNamesOfAnEnum) {
        size_t index = 99;
        EXPECT_EQ(reader.ReadEnumIndex(sources, Bind("mode"), index), PanelReadStatus::Ok);
        EXPECT_EQ(index, 1u);
        probe.inner.mode = PanelProbeMode::Failed;
        EXPECT_EQ(reader.ReadEnumIndex(sources, Bind("inner.mode"), index), PanelReadStatus::Ok);
        EXPECT_EQ(index, 2u);
        EXPECT_EQ(reader.ReadEnumIndex(sources, Bind("rows[1].mode"), index), PanelReadStatus::WrongType) << "a list position in the path is not read in place";

        Span<const StringView> names;
        EXPECT_EQ(reader.EnumNames(sources, Bind("mode"), names), PanelReadStatus::Ok);
        ASSERT_EQ(names.size(), 3u);
        EXPECT_EQ(names[0], "Idle");
        EXPECT_EQ(names[2], "Failed");
    }

    TEST_F(PanelBindingReadTest, AnEnumReadOfAnotherKindOrAMissingFieldIsAStatus) {
        size_t index = 99;
        Span<const StringView> names;
        EXPECT_EQ(reader.ReadEnumIndex(sources, Bind("count"), index), PanelReadStatus::WrongType);
        EXPECT_EQ(reader.ReadEnumIndex(sources, Bind("title"), index), PanelReadStatus::WrongType);
        EXPECT_EQ(reader.ReadEnumIndex(sources, Bind("nope"), index), PanelReadStatus::BadPath);
        EXPECT_EQ(reader.EnumNames(sources, Bind("flag"), names), PanelReadStatus::WrongType);
        EXPECT_EQ(reader.EnumNames(sources, Bind("nope"), names), PanelReadStatus::BadPath);
        EXPECT_EQ(index, 99u);
        EXPECT_TRUE(names.empty());

        const uint8_t outsideTheNames = 200;
        std::memcpy(&probe.mode, &outsideTheNames, sizeof(outsideTheNames));
        EXPECT_EQ(reader.ReadEnumIndex(sources, Bind("mode"), index), PanelReadStatus::BadValue);
        EXPECT_EQ(index, 99u);
    }

    TEST_F(PanelBindingReadTest, ASteadyReadAllocatesNothing) {
        for (size_t i = 0; i < kManyRows; ++i) {
            probe.rows.push_back(MakeRow("some/file.txt", static_cast<int32_t>(i), i % 2 == 0));
        }
        probe.flag = true;
        const PanelBinding rows = Bind("rows");
        const PanelBinding flag = Bind("flag");
        const PanelBinding count = Bind("count");
        const PanelBinding title = Bind("title");
        const PanelBinding tag = Bind("tag");
        const PanelBinding mode = Bind("mode");

        const auto readAll = [&] {
            bool boolean = false;
            double number = 0.0;
            StringView text;
            size_t listCount = 0;
            EXPECT_EQ(reader.ReadBool(sources, flag, boolean), PanelReadStatus::Ok);
            EXPECT_EQ(reader.ReadNumber(sources, count, number), PanelReadStatus::Ok);
            EXPECT_EQ(reader.ReadText(sources, title, scratch, text), PanelReadStatus::Ok);
            EXPECT_EQ(reader.ReadText(sources, tag, scratch, text), PanelReadStatus::Ok);
            EXPECT_EQ(reader.ReadText(sources, mode, scratch, text), PanelReadStatus::Ok);
            EXPECT_EQ(reader.ReadText(sources, count, scratch, text), PanelReadStatus::Ok);
            size_t index = 0;
            Span<const StringView> names;
            EXPECT_EQ(reader.ReadEnumIndex(sources, mode, index), PanelReadStatus::Ok);
            EXPECT_EQ(reader.EnumNames(sources, mode, names), PanelReadStatus::Ok);
            EXPECT_EQ(reader.ListCount(sources, rows, listCount), PanelReadStatus::Ok);
            for (size_t row = 0; row < listCount; ++row) {
                EXPECT_EQ(reader.ReadRowText(sources, rows, row, "path", scratch, text), PanelReadStatus::Ok);
                EXPECT_EQ(reader.ReadRowText(sources, rows, row, "size", scratch, text), PanelReadStatus::Ok);
                EXPECT_EQ(reader.ReadRowBool(sources, rows, row, "done", boolean), PanelReadStatus::Ok);
            }
        };
        readAll();

        ExpectNoAllocations(readAll);
    }

    namespace {

        class PanelBindingWriteTest : public PanelBindingReadTest {
        protected:
            PanelBindingWriter writer{reader};
        };

    }  // namespace

    TEST_F(PanelBindingWriteTest, WritesATopLevelAnEmbeddedAndARowString) {
        EXPECT_EQ(writer.WriteText(sources, Bind("title"), "Renamed"), PanelReadStatus::Ok);
        EXPECT_EQ(probe.title, "Renamed");
        EXPECT_EQ(Text("title"), "Renamed");

        EXPECT_EQ(writer.WriteText(sources, Bind("inner.path"), "deep"), PanelReadStatus::Ok);
        EXPECT_EQ(probe.inner.path, "deep");

        EXPECT_EQ(writer.WriteRowText(sources, Bind("rows"), 1, "path", "second"), PanelReadStatus::Ok);
        EXPECT_EQ(probe.rows[1].path, "second");
        EXPECT_EQ(probe.rows[0].path, "a.txt");
        StringView text;
        EXPECT_EQ(reader.ReadRowText(sources, Bind("rows"), 1, "path", scratch, text), PanelReadStatus::Ok);
        EXPECT_EQ(text, "second");

        EXPECT_EQ(writer.WriteRowText(sources, Bind("names"), 0, "", "first"), PanelReadStatus::Ok);
        EXPECT_EQ(probe.names[0], "first");
    }

    TEST_F(PanelBindingWriteTest, AFieldThatIsNotAnEngineStringIsWrongTypeAndStaysAsItWas) {
        EXPECT_EQ(writer.WriteText(sources, Bind("count"), "5"), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteText(sources, Bind("flag"), "true"), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteText(sources, Bind("code"), "other"), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteText(sources, Bind("mode"), "Idle"), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteText(sources, Bind("rows"), "x"), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteText(sources, Bind("maybe"), "1"), PanelReadStatus::WrongType);
        EXPECT_EQ(probe.count, kProbeCount);
        EXPECT_EQ(probe.code, "fixed");
    }

    TEST_F(PanelBindingWriteTest, APoolStringIsNeverInterned) {
        const String fresh = FreshText("panel_write");
        EXPECT_EQ(writer.WriteText(sources, Bind("tag"), fresh), PanelReadStatus::WrongType);
        EXPECT_EQ(probe.tag.ToStringView(), "tag-text");
        EXPECT_FALSE(PoolString::Find(StringView(fresh)).has_value());
    }

    TEST_F(PanelBindingWriteTest, AMissingSourcePathOrRowIsAStatus) {
        PanelBinding other = Bind("title");
        other.source = "nowhere";
        EXPECT_EQ(writer.WriteText(sources, other, "x"), PanelReadStatus::UnknownSource);
        EXPECT_EQ(writer.WriteText(sources, Bind("nope"), "x"), PanelReadStatus::BadPath);
        EXPECT_EQ(writer.WriteRowText(sources, Bind("rows"), 2, "path", "x"), PanelReadStatus::NoRow);
        EXPECT_EQ(writer.WriteRowText(sources, Bind("rows"), 0, "nope", "x"), PanelReadStatus::BadPath);
        EXPECT_EQ(writer.WriteRowText(sources, Bind("count"), 0, "path", "x"), PanelReadStatus::NotAList);
        EXPECT_EQ(writer.WriteRowText(sources, Bind("numbers"), 0, "", "x"), PanelReadStatus::WrongType);

        current = nullptr;
        EXPECT_EQ(writer.WriteText(sources, Bind("title"), "x"), PanelReadStatus::NoObject);
        EXPECT_EQ(writer.WriteRowText(sources, Bind("rows"), 0, "path", "x"), PanelReadStatus::NoObject);
    }

    TEST_F(PanelBindingWriteTest, AConstSourceObjectIsNotWritten) {
        const PanelProbe& frozen = probe;
        current = nullptr;
        const PanelSourceHandle constHandle = sources.Register(kSource, [&frozen] { return TypedRef::Of(frozen); });

        EXPECT_EQ(writer.WriteText(sources, Bind("title"), "x"), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteText(sources, Bind("inner.path"), "x"), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteRowText(sources, Bind("rows"), 0, "path", "x"), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteRowText(sources, Bind("names"), 0, "", "x"), PanelReadStatus::WrongType);
        EXPECT_EQ(probe.title, "Files");
        EXPECT_EQ(probe.rows[0].path, "a.txt");
        EXPECT_EQ(Text("title"), "Files") << "a const object is still read";
    }

    TEST_F(PanelBindingWriteTest, ASteadyWriteAllocatesNothingAndSharesTheReadersCache) {
        const PanelBinding title = Bind("title");
        const PanelBinding rows = Bind("rows");
        probe.title = String(64, 'a');
        probe.rows[0].path = String(64, 'b');
        const String same(32, 'c');

        const auto writeAll = [&] {
            EXPECT_EQ(writer.WriteText(sources, title, same), PanelReadStatus::Ok);
            EXPECT_EQ(writer.WriteRowText(sources, rows, 0, "path", same), PanelReadStatus::Ok);
        };
        writeAll();
        const size_t compiled = reader.CompiledCount();
        EXPECT_GT(compiled, 0u) << "the writer compiles its paths in the reader's cache";

        ExpectNoAllocations(writeAll);
        EXPECT_EQ(reader.CompiledCount(), compiled);
    }

    namespace {

        constexpr StringView kNumbersSource = "n";
        constexpr int16_t kNeighbourBefore = 0x1111;
        constexpr int16_t kNeighbourAfter = 0x2222;

        PanelBinding BindNumbers(StringView path) {
            PanelBinding binding;
            binding.source = String(kNumbersSource);
            binding.path = String(path);
            return binding;
        }

        /// Every scalar of the object, so a write that touched a neighbour shows as a difference
        auto Scalars(const PanelProbeNumbers& value) {
            return std::make_tuple(value.i8, value.u8, value.i16, value.u16, value.i32, value.u32, value.i64, value.u64, value.f32, value.f64, value.flag, value.mode, value.level, value.inner.before,
                                   value.inner.flag, value.inner.small, value.inner.ratio, value.inner.level, value.inner.after, value.rows[0].before, value.rows[0].small, value.rows[0].after,
                                   value.rows[1].before, value.rows[1].flag, value.rows[1].small, value.rows[1].ratio, value.rows[1].level, value.rows[1].after, value.numbers[1]);
        }

        class PanelBindingNumbersTest : public ::testing::Test {
        protected:
            void SetUp() override {
                values.inner.before = kNeighbourBefore;
                values.inner.after = kNeighbourAfter;
                values.rows.resize(2);
                for (PanelProbeCells& row : values.rows) {
                    row.before = kNeighbourBefore;
                    row.after = kNeighbourAfter;
                }
                values.numbers = {1, 2, 3};
                values.i8 = 1;
                values.u8 = 2;
                values.i16 = 3;
                values.u16 = 4;
                values.i32 = 5;
                values.u32 = 6;
                values.i64 = 7;
                values.u64 = 8;
                values.f32 = 9.0f;
                values.f64 = 10.0;
                handle = sources.Register(kNumbersSource, [this] { return current != nullptr ? TypedRef::Of(*current) : TypedRef{}; });
            }

            /// Writes @p value at @p path of the numbers and checks that the one field changed and no other
            template <typename TField>
            void ExpectWritten(StringView path, double value, TField PanelProbeNumbers::* field, TField expected) {
                const PanelProbeNumbers before = values;
                EXPECT_EQ(writer.WriteNumber(sources, BindNumbers(path), value), PanelReadStatus::Ok) << String(path).c_str();
                PanelProbeNumbers want = before;
                want.*field = expected;
                EXPECT_EQ(Scalars(values), Scalars(want)) << String(path).c_str() << " <- " << value;
            }

            PanelDataSources sources;
            PanelBindingReader reader;
            PanelBindingWriter writer{reader};
            PanelProbeNumbers values;
            PanelProbeNumbers* current = &values;
            PanelSourceHandle handle;
        };

        template <typename T>
        constexpr double Limit(T value) {
            return static_cast<double>(value);
        }

    }  // namespace

    TEST_F(PanelBindingNumbersTest, WritesABoolAtTheTopLevelEmbeddedAndInARow) {
        EXPECT_EQ(writer.WriteBool(sources, BindNumbers("flag"), true), PanelReadStatus::Ok);
        EXPECT_TRUE(values.flag);
        EXPECT_EQ(writer.WriteBool(sources, BindNumbers("inner.flag"), true), PanelReadStatus::Ok);
        EXPECT_TRUE(values.inner.flag);
        EXPECT_EQ(writer.WriteRowBool(sources, BindNumbers("rows"), 1, "flag", true), PanelReadStatus::Ok);
        EXPECT_TRUE(values.rows[1].flag);
        EXPECT_FALSE(values.rows[0].flag);
        EXPECT_EQ(writer.WriteBool(sources, BindNumbers("flag"), false), PanelReadStatus::Ok);
        EXPECT_FALSE(values.flag);
        EXPECT_EQ(values.inner.before, kNeighbourBefore);
        EXPECT_EQ(values.inner.after, kNeighbourAfter);
        EXPECT_EQ(values.rows[1].before, kNeighbourBefore);
        EXPECT_EQ(values.rows[1].after, kNeighbourAfter);
    }

    TEST_F(PanelBindingNumbersTest, WritesEveryNumberWidthAndLeavesItsNeighboursAlone) {
        ExpectWritten("i8", -7, &PanelProbeNumbers::i8, static_cast<int8_t>(-7));
        ExpectWritten("u8", 200, &PanelProbeNumbers::u8, static_cast<uint8_t>(200));
        ExpectWritten("i16", -30000, &PanelProbeNumbers::i16, static_cast<int16_t>(-30000));
        ExpectWritten("u16", 60000, &PanelProbeNumbers::u16, static_cast<uint16_t>(60000));
        ExpectWritten("i32", -2000000000, &PanelProbeNumbers::i32, -2000000000);
        ExpectWritten("u32", 4000000000.0, &PanelProbeNumbers::u32, 4000000000u);
        ExpectWritten("i64", -9000000000000000000.0, &PanelProbeNumbers::i64, int64_t{-9000000000000000000});
        ExpectWritten("u64", 18000000000000000000.0, &PanelProbeNumbers::u64, uint64_t{18000000000000000000u});
        ExpectWritten("f32", 0.5, &PanelProbeNumbers::f32, 0.5f);
        ExpectWritten("f64", 1.0e100, &PanelProbeNumbers::f64, 1.0e100);
    }

    TEST_F(PanelBindingNumbersTest, AnIntegerIsRoundedAndAFloatIsNot) {
        ExpectWritten("i32", 2.4, &PanelProbeNumbers::i32, 2);
        ExpectWritten("i32", 2.5, &PanelProbeNumbers::i32, 3);
        ExpectWritten("i32", -2.5, &PanelProbeNumbers::i32, -3);
        ExpectWritten("f64", 2.4, &PanelProbeNumbers::f64, 2.4);
    }

    TEST_F(PanelBindingNumbersTest, AValueIsClampedToTheFieldsTypeAtBothEnds) {
        ExpectWritten("i8", 1000, &PanelProbeNumbers::i8, std::numeric_limits<int8_t>::max());
        ExpectWritten("i8", -1000, &PanelProbeNumbers::i8, std::numeric_limits<int8_t>::lowest());
        ExpectWritten("u8", 300, &PanelProbeNumbers::u8, std::numeric_limits<uint8_t>::max());
        ExpectWritten("u8", -5, &PanelProbeNumbers::u8, uint8_t{0});
        ExpectWritten("i16", 1.0e6, &PanelProbeNumbers::i16, std::numeric_limits<int16_t>::max());
        ExpectWritten("i16", -1.0e6, &PanelProbeNumbers::i16, std::numeric_limits<int16_t>::lowest());
        ExpectWritten("u16", 1.0e6, &PanelProbeNumbers::u16, std::numeric_limits<uint16_t>::max());
        ExpectWritten("u16", -1.0, &PanelProbeNumbers::u16, uint16_t{0});
        ExpectWritten("i32", 3.0e9, &PanelProbeNumbers::i32, std::numeric_limits<int32_t>::max());
        ExpectWritten("i32", -3.0e9, &PanelProbeNumbers::i32, std::numeric_limits<int32_t>::lowest());
        ExpectWritten("u32", 5.0e9, &PanelProbeNumbers::u32, std::numeric_limits<uint32_t>::max());
        ExpectWritten("u32", -1.0, &PanelProbeNumbers::u32, uint32_t{0});
        ExpectWritten("i64", 1.0e30, &PanelProbeNumbers::i64, std::numeric_limits<int64_t>::max());
        ExpectWritten("i64", -1.0e30, &PanelProbeNumbers::i64, std::numeric_limits<int64_t>::lowest());
        ExpectWritten("i64", Limit(std::numeric_limits<int64_t>::max()), &PanelProbeNumbers::i64, std::numeric_limits<int64_t>::max());
        ExpectWritten("u64", 1.0e30, &PanelProbeNumbers::u64, std::numeric_limits<uint64_t>::max());
        ExpectWritten("u64", -1.0e30, &PanelProbeNumbers::u64, uint64_t{0});
        ExpectWritten("u64", Limit(std::numeric_limits<uint64_t>::max()), &PanelProbeNumbers::u64, std::numeric_limits<uint64_t>::max());
        ExpectWritten("f32", 1.0e300, &PanelProbeNumbers::f32, std::numeric_limits<float>::max());
        ExpectWritten("f32", -1.0e300, &PanelProbeNumbers::f32, std::numeric_limits<float>::lowest());
    }

    TEST_F(PanelBindingNumbersTest, NotANumberAndInfinityAreRefusedAndWriteNothing) {
        const auto before = Scalars(values);
        for (const StringView path : {"i8", "u64", "f32", "f64"}) {
            EXPECT_EQ(writer.WriteNumber(sources, BindNumbers(path), std::numeric_limits<double>::quiet_NaN()), PanelReadStatus::BadValue) << String(path).c_str();
            EXPECT_EQ(writer.WriteNumber(sources, BindNumbers(path), std::numeric_limits<double>::infinity()), PanelReadStatus::BadValue) << String(path).c_str();
            EXPECT_EQ(writer.WriteNumber(sources, BindNumbers(path), -std::numeric_limits<double>::infinity()), PanelReadStatus::BadValue) << String(path).c_str();
        }
        EXPECT_EQ(Scalars(values), before);
    }

    TEST_F(PanelBindingNumbersTest, WritesAnEnumByTheIndexOfItsNameInOneAndFourBytes) {
        EXPECT_EQ(writer.WriteEnumIndex(sources, BindNumbers("mode"), 2), PanelReadStatus::Ok);
        EXPECT_EQ(values.mode, PanelProbeMode::Failed);
        EXPECT_EQ(writer.WriteEnumIndex(sources, BindNumbers("level"), 3), PanelReadStatus::Ok);
        EXPECT_EQ(values.level, PanelProbeLevel::Max);
        EXPECT_EQ(writer.WriteEnumIndex(sources, BindNumbers("inner.level"), 2), PanelReadStatus::Ok);
        EXPECT_EQ(values.inner.level, PanelProbeLevel::High);
        EXPECT_EQ(writer.WriteRowEnumIndex(sources, BindNumbers("rows"), 1, "level", 1), PanelReadStatus::Ok);
        EXPECT_EQ(values.rows[1].level, PanelProbeLevel::Mid);
        EXPECT_EQ(values.rows[0].level, PanelProbeLevel::Low);
        EXPECT_EQ(writer.WriteEnumIndex(sources, BindNumbers("level"), 0), PanelReadStatus::Ok);
        EXPECT_EQ(values.level, PanelProbeLevel::Low) << "the high bytes of a four byte enum are written too";

        EXPECT_EQ(values.inner.after, kNeighbourAfter);
        EXPECT_EQ(values.rows[1].after, kNeighbourAfter);
        EXPECT_EQ(values.u64, 8u);
        size_t index = 99;
        EXPECT_EQ(reader.ReadEnumIndex(sources, BindNumbers("mode"), index), PanelReadStatus::Ok);
        EXPECT_EQ(index, 2u);
    }

    TEST_F(PanelBindingNumbersTest, AnEnumIndexPastTheNamesIsRefusedAndWritesNothing) {
        values.mode = PanelProbeMode::Busy;
        values.level = PanelProbeLevel::High;
        EXPECT_EQ(writer.WriteEnumIndex(sources, BindNumbers("mode"), 3), PanelReadStatus::BadValue);
        EXPECT_EQ(writer.WriteEnumIndex(sources, BindNumbers("level"), 4), PanelReadStatus::BadValue);
        EXPECT_EQ(writer.WriteEnumIndex(sources, BindNumbers("level"), std::numeric_limits<size_t>::max()), PanelReadStatus::BadValue);
        EXPECT_EQ(writer.WriteRowEnumIndex(sources, BindNumbers("rows"), 0, "level", 4), PanelReadStatus::BadValue);
        EXPECT_EQ(values.mode, PanelProbeMode::Busy);
        EXPECT_EQ(values.level, PanelProbeLevel::High);
        EXPECT_EQ(values.rows[0].level, PanelProbeLevel::Low);
    }

    TEST_F(PanelBindingNumbersTest, AFieldOfAnotherKindIsWrongTypeForEachWriteAndStaysAsItWas) {
        const auto before = Scalars(values);
        for (const StringView path : {"flag", "mode", "level", "text", "rows", "maybe", "inner"}) {
            EXPECT_EQ(writer.WriteNumber(sources, BindNumbers(path), 1.0), PanelReadStatus::WrongType) << String(path).c_str();
        }
        for (const StringView path : {"i32", "f32", "mode", "text", "rows", "maybe", "inner"}) {
            EXPECT_EQ(writer.WriteBool(sources, BindNumbers(path), true), PanelReadStatus::WrongType) << String(path).c_str();
        }
        for (const StringView path : {"i32", "f32", "flag", "text", "rows", "maybe", "inner"}) {
            EXPECT_EQ(writer.WriteEnumIndex(sources, BindNumbers(path), 1), PanelReadStatus::WrongType) << String(path).c_str();
        }
        EXPECT_EQ(writer.WriteNumber(sources, BindNumbers("text"), std::numeric_limits<double>::quiet_NaN()), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteRowBool(sources, BindNumbers("numbers"), 0, "", true), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteRowEnumIndex(sources, BindNumbers("numbers"), 0, "", 1), PanelReadStatus::WrongType);
        EXPECT_EQ(Scalars(values), before);
        EXPECT_TRUE(values.text.empty());
        EXPECT_FALSE(values.maybe.has_value());
    }

    TEST_F(PanelBindingNumbersTest, WritesANumberOfAnEmbeddedObjectARowAndAListOfScalars) {
        EXPECT_EQ(writer.WriteNumber(sources, BindNumbers("inner.ratio"), 0.25), PanelReadStatus::Ok);
        EXPECT_EQ(values.inner.ratio, 0.25f);
        EXPECT_EQ(writer.WriteRowNumber(sources, BindNumbers("rows"), 1, "small", 300), PanelReadStatus::Ok);
        EXPECT_EQ(values.rows[1].small, std::numeric_limits<uint8_t>::max());
        EXPECT_EQ(values.rows[1].before, kNeighbourBefore);
        EXPECT_EQ(values.rows[1].ratio, 0.0f);
        EXPECT_EQ(values.rows[0].small, 0);
        EXPECT_EQ(writer.WriteRowNumber(sources, BindNumbers("numbers"), 1, "", 40.0), PanelReadStatus::Ok);
        EXPECT_EQ(values.numbers, (Vector<int32_t>{1, 40, 3}));

        double number = 0.0;
        EXPECT_EQ(reader.ReadNumber(sources, BindNumbers("inner.ratio"), number), PanelReadStatus::Ok);
        EXPECT_EQ(number, 0.25);
    }

    TEST_F(PanelBindingNumbersTest, AMissingSourcePathOrRowIsAStatusForEveryWrite) {
        PanelBinding other = BindNumbers("i32");
        other.source = "nowhere";
        EXPECT_EQ(writer.WriteNumber(sources, other, 1.0), PanelReadStatus::UnknownSource);
        EXPECT_EQ(writer.WriteBool(sources, other, true), PanelReadStatus::UnknownSource);
        EXPECT_EQ(writer.WriteEnumIndex(sources, other, 1), PanelReadStatus::UnknownSource);
        EXPECT_EQ(writer.WriteNumber(sources, BindNumbers("nope"), 1.0), PanelReadStatus::BadPath);
        EXPECT_EQ(writer.WriteRowNumber(sources, BindNumbers("rows"), 2, "small", 1.0), PanelReadStatus::NoRow);
        EXPECT_EQ(writer.WriteRowBool(sources, BindNumbers("rows"), 0, "nope", true), PanelReadStatus::BadPath);
        EXPECT_EQ(writer.WriteRowEnumIndex(sources, BindNumbers("i32"), 0, "level", 1), PanelReadStatus::NotAList);

        current = nullptr;
        EXPECT_EQ(writer.WriteNumber(sources, BindNumbers("i32"), 1.0), PanelReadStatus::NoObject);
        EXPECT_EQ(writer.WriteRowBool(sources, BindNumbers("rows"), 0, "flag", true), PanelReadStatus::NoObject);
    }

    TEST_F(PanelBindingNumbersTest, AConstSourceObjectIsNotWritten) {
        const PanelProbeNumbers& frozen = values;
        current = nullptr;
        const PanelSourceHandle constHandle = sources.Register(kNumbersSource, [&frozen] { return TypedRef::Of(frozen); });
        const auto before = Scalars(values);

        EXPECT_EQ(writer.WriteNumber(sources, BindNumbers("i32"), 99.0), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteBool(sources, BindNumbers("flag"), true), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteEnumIndex(sources, BindNumbers("level"), 1), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteNumber(sources, BindNumbers("inner.ratio"), 1.0), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteRowNumber(sources, BindNumbers("rows"), 0, "small", 1.0), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteRowBool(sources, BindNumbers("rows"), 0, "flag", true), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteRowEnumIndex(sources, BindNumbers("rows"), 0, "level", 1), PanelReadStatus::WrongType);
        EXPECT_EQ(writer.WriteRowNumber(sources, BindNumbers("numbers"), 0, "", 9.0), PanelReadStatus::WrongType);
        EXPECT_EQ(Scalars(values), before);
    }

    TEST_F(PanelBindingNumbersTest, ASteadyWriteOfEveryKindAllocatesNothing) {
        const PanelBinding flag = BindNumbers("flag");
        const PanelBinding count = BindNumbers("i32");
        const PanelBinding level = BindNumbers("level");
        const PanelBinding rows = BindNumbers("rows");

        const auto writeAll = [&] {
            EXPECT_EQ(writer.WriteBool(sources, flag, true), PanelReadStatus::Ok);
            EXPECT_EQ(writer.WriteNumber(sources, count, 5.0), PanelReadStatus::Ok);
            EXPECT_EQ(writer.WriteEnumIndex(sources, level, 2), PanelReadStatus::Ok);
            EXPECT_EQ(writer.WriteRowBool(sources, rows, 1, "flag", true), PanelReadStatus::Ok);
            EXPECT_EQ(writer.WriteRowNumber(sources, rows, 1, "ratio", 0.5), PanelReadStatus::Ok);
            EXPECT_EQ(writer.WriteRowEnumIndex(sources, rows, 1, "level", 3), PanelReadStatus::Ok);
        };
        writeAll();
        const size_t compiled = reader.CompiledCount();

        ExpectNoAllocations(writeAll);
        EXPECT_EQ(reader.CompiledCount(), compiled);
    }

}  // namespace BECore::Tests
