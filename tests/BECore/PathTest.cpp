#include <BECore/File/Path.h>
#include <Common/NativePath.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        /// What the view helpers and the members of Path give for one text; the standard library's path gives the same on POSIX (the oracle below)
        struct PathRow {
            StringView text;
            StringView parent;
            StringView filename;
            StringView stem;
            StringView extension;
            StringView normal;
            bool absolute;
        };

        const Vector<PathRow>& Rows() {
            static const Vector<PathRow> rows{
                {"", "", "", "", "", "", false},
                {"/", "/", "", "", "", "/", true},
                {"//a", "/", "a", "a", "", "/a", true},
                {"a", "", "a", "a", "", "a", false},
                {"a/", "a", "", "", "", "a/", false},
                {"a//b", "a", "b", "b", "", "a/b", false},
                {"./a", ".", "a", "a", "", "a", false},
                {"../a", "..", "a", "a", "", "../a", false},
                {"a/../b", "a/..", "b", "b", "", "b", false},
                {"a/b/..", "a/b", "..", "..", "", "a/", false},
                {"a/..", "a", "..", "..", "", ".", false},
                {"a/.", "a", ".", ".", "", "a/", false},
                {"a/./", "a/.", "", "", "", "a/", false},
                {".", "", ".", ".", "", ".", false},
                {"..", "", "..", "..", "", "..", false},
                {"/..", "/", "..", "..", "", "/", true},
                {"../..", "..", "..", "..", "", "../..", false},
                {"../", "..", "", "", "", "..", false},
                {"../a/..", "../a", "..", "..", "", "..", false},
                {"../../", "../..", "", "", "", "../..", false},
                {"a/b/", "a/b", "", "", "", "a/b/", false},
                {".hidden", "", ".hidden", ".hidden", "", ".hidden", false},
                {"dir/.hidden.txt", "dir", ".hidden.txt", ".hidden", ".txt", "dir/.hidden.txt", false},
                {"a.b.c", "", "a.b.c", "a.b", ".c", "a.b.c", false},
                {"a.", "", "a.", "a", ".", "a.", false},
                {"noext", "", "noext", "noext", "", "noext", false},
                {"/usr/lib/x.so", "/usr/lib", "x.so", "x", ".so", "/usr/lib/x.so", true},
                {"/a/", "/a", "", "", "", "/a/", true},
                {"é/ü.ñ", "é", "ü.ñ", "ü", ".ñ", "é/ü.ñ", false},
                {"/папка/файл.тхт", "/папка", "файл.тхт", "файл", ".тхт", "/папка/файл.тхт", true},
                {"日本/語.日本/名前", "日本/語.日本", "名前", "名前", "", "日本/語.日本/名前", false},
                {"é.é/é.", "é.é", "é.", "é", ".", "é.é/é.", false},
                {"a/é/../ü", "a/é/..", "ü", "ü", "", "a/ü", false},
            };
            return rows;
        }

#ifndef _WIN32
        /// The standard library's own answer: the oracle of the table (POSIX only, the Windows rules differ and are listed apart)
        auto Oracle(StringView text) {
            return NativePath(Path(text));
        }

        template <typename Native>
        String Text(const Native& path) {
            return String(FromNativePath(path).View());
        }
#endif

        int Sign(int value) {
            return (value > 0) - (value < 0);
        }

    }  // namespace

    TEST(PathTest, ViewHelpersGiveTheTableValues) {
        for (const PathRow& row : Rows()) {
            SCOPED_TRACE(String(row.text).c_str());
            EXPECT_EQ(PathParent(row.text), row.parent);
            EXPECT_EQ(PathFilename(row.text), row.filename);
            EXPECT_EQ(PathStem(row.text), row.stem);
            EXPECT_EQ(PathExtension(row.text), row.extension);
            EXPECT_EQ(PathIsAbsolute(row.text), row.absolute);
        }
    }

    TEST(PathTest, MembersGiveTheTableValues) {
        for (const PathRow& row : Rows()) {
            SCOPED_TRACE(String(row.text).c_str());
            const Path path(row.text);
            EXPECT_EQ(path.Parent().View(), row.parent);
            EXPECT_EQ(path.Filename().View(), row.filename);
            EXPECT_EQ(path.Stem().View(), row.stem);
            EXPECT_EQ(path.Extension().View(), row.extension);
            EXPECT_EQ(path.LexicallyNormal().View(), row.normal);
            EXPECT_EQ(path.IsAbsolute(), row.absolute);
        }
    }

    TEST(PathTest, ViewHelpersReturnViewsIntoTheArgument) {
        const String text = "dir/sub/file.txt";
        const StringView view = text;
        const auto inside = [&](StringView part) { return part.data() >= view.data() && part.data() + part.size() <= view.data() + view.size(); };
        EXPECT_TRUE(inside(PathParent(view)));
        EXPECT_TRUE(inside(PathFilename(view)));
        EXPECT_TRUE(inside(PathStem(view)));
        EXPECT_TRUE(inside(PathExtension(view)));
    }

#ifndef _WIN32
    TEST(PathTest, TableMatchesStdFilesystem) {
        for (const PathRow& row : Rows()) {
            SCOPED_TRACE(String(row.text).c_str());
            const auto oracle = Oracle(row.text);
            // libc++ keeps the whole "//" of a path that starts with two separators as its root; POSIX leaves that to the implementation, we say "/"
            const bool doubledRoot = row.text.starts_with("//");
            if (!doubledRoot) {
                EXPECT_EQ(String(PathParent(row.text)), Text(oracle.parent_path()));
            }
            EXPECT_EQ(String(PathFilename(row.text)), Text(oracle.filename()));
            EXPECT_EQ(String(PathStem(row.text)), Text(oracle.stem()));
            EXPECT_EQ(String(PathExtension(row.text)), Text(oracle.extension()));
            EXPECT_EQ(PathIsAbsolute(row.text), oracle.is_absolute());
            const String normal = Text(oracle.lexically_normal());
            EXPECT_EQ(String(Path(row.text).LexicallyNormal().View()), row.text.empty() ? String() : normal);
        }
    }
#endif

    namespace {

        struct CompareRow {
            StringView left;
            StringView right;
            int order;
        };

        const Vector<CompareRow>& CompareRows() {
            static const Vector<CompareRow> rows{
                {"a", "a", 0},        {"", "", 0},         {"", "a", -1},      {"a/b", "a-b", -1}, {"a/b", "a.txt", -1},     {"a-b", "a.txt", -1}, {"a//b", "a/b", 0},
                {"a/b", "a/b/c", -1}, {"a/c", "a/b/c", 1}, {"a", "a/", -1},    {"a/", "a//", 0},   {"a", "/a", -1},          {"/a", "a", 1},       {"/a", "//a", 0},
                {"/", "/a", -1},      {"é", "z", 1},       {"é/a", "é.a", -1}, {"é", "ê", -1},     {"日本/a", "日本/b", -1}, {"a/é", "a/e", 1},
            };
            return rows;
        }

    }  // namespace

    TEST(PathTest, OrderIsByComponentThenByte) {
        for (const CompareRow& row : CompareRows()) {
            SCOPED_TRACE(Format("{} <=> {}", row.left, row.right).c_str());
            EXPECT_EQ(Sign(PathCompare(row.left, row.right)), row.order);
            EXPECT_EQ(Sign(PathCompare(row.right, row.left)), -row.order);
            const Path left(row.left);
            const Path right(row.right);
            EXPECT_EQ(left == right, row.order == 0);
            EXPECT_EQ(left < right, row.order < 0);
            EXPECT_EQ(left > right, row.order > 0);
        }
    }

#ifndef _WIN32
    TEST(PathTest, OrderMatchesStdFilesystem) {
        for (const CompareRow& row : CompareRows()) {
            SCOPED_TRACE(Format("{} <=> {}", row.left, row.right).c_str());
            EXPECT_EQ(Sign(Oracle(row.left).compare(Oracle(row.right))), row.order);
        }
    }
#endif

    TEST(PathTest, EqualPathsHashAlikeAndFindEachOtherInAMap) {
        HashMap<Path, int> map;
        map[Path("é/b")] = 1;
        map[Path("a/b")] = 2;
        map[Path("/x")] = 3;

        ASSERT_TRUE(map.contains(Path("a//b")));
        EXPECT_EQ(map.find(Path("a//b"))->second, 2);
        ASSERT_TRUE(map.contains(Path("é///b")));
        EXPECT_EQ(map.find(Path("é///b"))->second, 1);
        EXPECT_TRUE(map.contains(Path("//x")));
        EXPECT_FALSE(map.contains(Path("x")));
        EXPECT_FALSE(map.contains(Path("a/b/")));
        EXPECT_EQ(PathHash("a//b"), PathHash("a/b"));
    }

    namespace {

        struct JoinRow {
            StringView left;
            StringView right;
            StringView joined;
        };

        const Vector<JoinRow>& JoinRows() {
            static const Vector<JoinRow> rows{
                {"a", "b", "a/b"},
                {"a/", "b", "a/b"},
                {"a//", "b", "a//b"},
                {"", "b", "b"},
                {"a", "", "a/"},
                {"", "", ""},
                {"a", "/b", "/b"},
                {"/", "b", "/b"},
                {"/a", "b/c", "/a/b/c"},
                {"a", "../b", "a/../b"},
                {"é", "ü.ñ", "é/ü.ñ"},
                {"日本", "/語", "/語"},
                {"папка/", "файл", "папка/файл"},
            };
            return rows;
        }

    }  // namespace

    TEST(PathTest, JoinPutsOneSeparatorAndAnAbsoluteRightSideReplaces) {
        for (const JoinRow& row : JoinRows()) {
            SCOPED_TRACE(Format("{} / {}", row.left, row.right).c_str());
            EXPECT_EQ((Path(row.left) / Path(row.right)).View(), row.joined);
            Path accumulated(row.left);
            accumulated /= Path(row.right);
            EXPECT_EQ(accumulated.View(), row.joined);
#ifndef _WIN32
            EXPECT_EQ(String(row.joined), Text(Oracle(row.left) / Oracle(row.right)));
#endif
        }
    }

    TEST(PathTest, JoinTakesEveryTextTypeOnTheRightWithoutAmbiguity) {
        const Path base("root");
        const String text = "s";
        const StringView view = "v";

        EXPECT_EQ((base / "lit").View(), "root/lit");
        EXPECT_EQ((base / text).View(), "root/s");
        EXPECT_EQ((base / view).View(), "root/v");
        EXPECT_EQ((base / Path("p")).View(), "root/p");

        Path accumulated = base;
        accumulated /= "lit";
        accumulated /= text;
        accumulated /= view;
        EXPECT_EQ(accumulated.View(), "root/lit/s/v");
    }

    TEST(PathTest, JoinOfAPathWithItselfDoublesIt) {
        Path path("a/b");
        path /= path;

        EXPECT_EQ(path.View(), "a/b/a/b");
    }

    namespace {

        struct RelativeRow {
            StringView path;
            StringView base;
            StringView relative;
        };

        const Vector<RelativeRow>& RelativeRows() {
            static const Vector<RelativeRow> rows{
                {"a/b/c", "a/b", "c"},
                {"a/b/c/d", "a/b", "c/d"},
                {"a/bc", "a/b", ""},
                {"a/b", "a/b", ""},
                {"a", "a/b", ""},
                {"a/b//c", "a/b", "c"},
                {"a/b/c", "a/b/", "c"},
                {"/x/y", "/", "x/y"},
                {"a/b", "", ""},
                {"/a", "", "a"},
                {"é/ü/ñ", "é", "ü/ñ"},
                {"é/üñ", "é/ü", ""},
                {"日本/語/名前", "日本/語", "名前"},
            };
            return rows;
        }

    }  // namespace

    TEST(PathTest, RelativeToIsThePartBelowTheBaseFolder) {
        for (const RelativeRow& row : RelativeRows()) {
            SCOPED_TRACE(Format("{} below {}", row.path, row.base).c_str());
            EXPECT_EQ(PathRelativeTo(row.path, row.base), row.relative);
            EXPECT_EQ(Path(row.path).Relative(Path(row.base)).View(), row.relative);
        }
    }

    TEST(PathTest, ReplaceExtension) {
        struct Row {
            StringView text;
            StringView extension;
            StringView result;
        };
        const Vector<Row> rows{
            {"a/b.txt", ".md", "a/b.md"},   {"a/b.txt", "md", "a/b.md"}, {"a/b.txt", "", "a/b"},         {"a/b", "md", "a/b.md"},
            {".hidden", ".x", ".hidden.x"}, {"a.b.c", ".d", "a.b.d"},    {"é/ü.ñ", ".日本", "é/ü.日本"}, {"a/", ".x", "a/.x"},
        };
        for (const Row& row : rows) {
            SCOPED_TRACE(String(row.text).c_str());
            EXPECT_EQ(Path(row.text).ReplaceExtension(row.extension).View(), row.result);
#ifndef _WIN32
            auto oracle = Oracle(row.text);
            oracle.replace_extension(ToStdString(row.extension));  //-NoStd: the oracle
            EXPECT_EQ(String(row.result), Text(oracle));
#endif
        }
    }

    TEST(PathTest, CStrIsTheTextEndedByAZero) {
        const Path path("caf\xC3\xA9/a b.png");
        ASSERT_NE(path.CStr(), nullptr);
        EXPECT_EQ(StringView(path.CStr()), path.View());
        EXPECT_EQ(path.CStr()[path.View().size()], '\0');
        EXPECT_EQ(*Path().CStr(), '\0');
    }

    TEST(PathTest, StoresTheTextAsGivenAndClearsAndImplicitlyViews) {
        Path path("a//./b");
        EXPECT_EQ(path.View(), "a//./b");
        EXPECT_FALSE(path.Empty());
        const StringView asView = path;
        EXPECT_EQ(asView, "a//./b");

        path.Clear();
        EXPECT_TRUE(path.Empty());
        EXPECT_EQ(Path(String("x/y")).View(), "x/y");
        EXPECT_EQ(Path(StringView("x/y")).View(), "x/y");
    }

    TEST(PathTest, NativePathRoundTripKeepsNonAsciiNames) {
        for (const StringView text : {StringView("dir/file.txt"), StringView("/папка/файл.тхт"), StringView("日本/語.日本"), StringView("é/ü.ñ"), StringView("")}) {
            SCOPED_TRACE(String(text).c_str());
            const Path path(text);
            EXPECT_TRUE(FromNativePath(NativePath(path)) == path);
        }
    }

#ifdef _WIN32
    TEST(PathTest, WindowsBackslashesBecomeSlashesAndDrivesAreRoots) {
        EXPECT_EQ(Path("C:\\a\\b").View(), "C:/a/b");
        EXPECT_EQ(Path("C:\\a\\b").Parent().View(), "C:/a");
        EXPECT_TRUE(Path("C:\\a").IsAbsolute());
        EXPECT_TRUE(Path("C:/").IsAbsolute());
        EXPECT_FALSE(Path("C:").IsAbsolute());
        EXPECT_FALSE(Path("\\a").IsAbsolute());
        EXPECT_EQ(Path("C:\\").Parent().View(), "C:/");
        EXPECT_EQ(Path("C:\\").Filename().View(), "");
        EXPECT_EQ((Path("C:") / "a").View(), "C:a");
        EXPECT_EQ((Path("C:/x") / "D:/y").View(), "D:/y");
        EXPECT_EQ(Path("C:/a/../b").LexicallyNormal().View(), "C:/b");
        EXPECT_EQ(Path("\\\\host\\share\\f").Filename().View(), "f");
        EXPECT_EQ(Path("a\\b") == Path("a/b"), true);
    }
#endif

}  // namespace BECore::Tests
