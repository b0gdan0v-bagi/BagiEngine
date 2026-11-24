#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/Console/EditorLauncher.h>

namespace BECore::Tests {

    namespace {

        // The arguments joined with '|', or "<none>" when there is no launch
        String Build(StringView launchTemplate, StringView file, uint32_t line = 42) {
            const Optional<Vector<String>> arguments = BuildEditorLaunch(launchTemplate, file, line);
            if (!arguments) {
                return "<none>";
            }
            String joined;
            for (const String& argument : *arguments) {
                if (!joined.empty()) {
                    joined += "|";
                }
                joined += argument;
            }
            return joined;
        }

        size_t CountOf(StringView launchTemplate, StringView file) {
            const Optional<Vector<String>> arguments = BuildEditorLaunch(launchTemplate, file, 1);
            return arguments ? arguments->size() : 0;
        }

    }  // namespace

    TEST(EditorLauncherTest, TheUsualTemplateGivesTheProgramTheSwitchAndFileWithLine) {
        EXPECT_EQ(Build("code -g {file}:{line}", "C:\\x\\y.cpp"), "code|-g|C:\\x\\y.cpp:42");
    }

    TEST(EditorLauncherTest, APathWithSpacesStaysOneArgument) {
        EXPECT_EQ(Build("code -g {file}:{line}", "C:\\My Project\\a b.cpp"), "code|-g|C:\\My Project\\a b.cpp:42");
        EXPECT_EQ(CountOf("code -g {file}:{line}", "C:\\My Project\\a b.cpp"), 3u);
    }

    TEST(EditorLauncherTest, ShellCharactersAndQuotesOfThePathAreNotInterpreted) {
        for (const char* path : {"/tmp/a\"b.cpp", "/tmp/a;b.cpp", "/tmp/a&b.cpp", "/tmp/$(x).cpp", "/tmp/a b\" c.cpp", "/tmp/{line}.cpp"}) {
            const Optional<Vector<String>> arguments = BuildEditorLaunch("editor {file}", path, 7);
            ASSERT_TRUE(arguments.has_value()) << path;
            ASSERT_EQ(arguments->size(), 2u) << path;
            EXPECT_EQ((*arguments)[1], path);
        }
    }

    TEST(EditorLauncherTest, QuotesKeepAWordOfTheTemplateTogether) {
        EXPECT_EQ(Build("\"C:\\Program Files\\Code\\code.exe\" -g {file}:{line}", "C:\\x.cpp"), "C:\\Program Files\\Code\\code.exe|-g|C:\\x.cpp:42");
        EXPECT_EQ(Build("editor \"--open={file}\"", "/a b"), "editor|--open=/a b");
    }

    TEST(EditorLauncherTest, TheTokensAreReplacedEverywhereInAnArgument) {
        EXPECT_EQ(Build("diff {file} {file}:{line}", "/a.cpp", 3), "diff|/a.cpp|/a.cpp:3");
        EXPECT_EQ(Build("e {line}{line}", "/a.cpp", 5), "e|55");
    }

    TEST(EditorLauncherTest, ATemplateWithoutALineTokenGetsNoLine) {
        EXPECT_EQ(Build("notepad {file}", "C:\\a.cpp"), "notepad|C:\\a.cpp");
    }

    TEST(EditorLauncherTest, ATemplateThatIsOnlyTheFileIsAProgramToRun) {
        EXPECT_EQ(Build("{file}", "/bin/tool"), "/bin/tool");
    }

    TEST(EditorLauncherTest, AnEmptyOrBlankTemplateGivesNothing) {
        EXPECT_EQ(Build("", "/a"), "<none>");
        EXPECT_EQ(Build("   \t ", "/a"), "<none>");
    }

    TEST(EditorLauncherTest, AnUnterminatedOrMisplacedQuoteIsAnError) {
        EXPECT_EQ(Build("code \"-g {file}", "/a"), "<none>");
        EXPECT_EQ(Build("co\"de -g {file}", "/a"), "<none>");
        EXPECT_EQ(Build("\"code\"x {file}", "/a"), "<none>");
    }

    TEST(EditorLauncherTest, FileUrlOfEncodesWhatIsNotUnreserved) {
        EXPECT_EQ(FileUrlOf("D:\\a b\\x.cpp"), "file:///D:/a%20b/x.cpp");
        EXPECT_EQ(FileUrlOf("D:/a/x.cpp"), "file:///D:/a/x.cpp");
        EXPECT_EQ(FileUrlOf("/home/me/x.cpp"), "file:///home/me/x.cpp");
        EXPECT_EQ(FileUrlOf("/a#b%c.cpp"), "file:///a%23b%25c.cpp");
        EXPECT_EQ(FileUrlOf("/\xC3\xA9.cpp"), "file:///%C3%A9.cpp");
        EXPECT_EQ(FileUrlOf("/a-b_c.d~e"), "file:///a-b_c.d~e");
    }

    TEST(EditorLauncherTest, ASiteIsOpenableOnlyForAnExistingAbsolutePath) {
        // The answer is cached per pointer for the life of the process, so every path asked about lives as long
        static const String missing(Path(TestTempPath("BagiEditorLauncherMissing") / "nothing.cpp").View());
        EXPECT_FALSE(IsSiteOpenable(nullptr));
        EXPECT_FALSE(IsSiteOpenable("relative/path.cpp"));
        EXPECT_FALSE(IsSiteOpenable(missing.c_str()));

        const char* thisFile = __FILE__;
        if (Path(thisFile).IsAbsolute() && PathExists(thisFile)) {
            EXPECT_TRUE(IsSiteOpenable(thisFile));
        }
    }

    TEST(EditorLauncherTest, TheAnswerForASiteIsAskedOfTheDiskOnce) {
        const Path folder(TestTempPath("BagiEditorLauncherSite"));
        static const String site((folder / "site.cpp").View());
        WriteFile(folder / "site.cpp", "x");
        EXPECT_TRUE(IsSiteOpenable(site.c_str()));
        RemoveDirectory(folder);
        EXPECT_TRUE(IsSiteOpenable(site.c_str()));
    }

    TEST(EditorLauncherTest, ASiteUnderANonAsciiFolderIsOpenable) {
        const Path folder = Path(TestTempPath("BagiEditorLauncherUtf8")) / "caf\xC3\xA9";
        WriteFile(folder / "site.cpp", "x");
        static const String site((folder / "site.cpp").View());
        static const String missing((folder / "nothing.cpp").View());
        EXPECT_TRUE(IsSiteOpenable(site.c_str()));
        EXPECT_FALSE(IsSiteOpenable(missing.c_str()));
        RemoveDirectory(folder.Parent());
    }

}  // namespace BECore::Tests
