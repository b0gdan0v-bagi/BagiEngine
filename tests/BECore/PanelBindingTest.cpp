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
#include <gtest/gtest.h>

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

}  // namespace BECore::Tests
