#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/FileSystem/PackagedContent.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // The names are literals on purpose: tools/export/ writes them, so a rename on one side only has to fail here
        void WriteContent(const Path& package) {
            WriteFile(package / "content/engine/config/A.xml", "<engine/>");
            WriteFile(package / "content/game/config/A.xml", "<game/>");
        }

    }  // namespace

    TEST(PackagedContentTest, MarkedFolderGivesItsContentWithTheGameAboveTheEngine) {
        const TestTempFolder folder("PackagedContentMarked");
        const Path package(folder.PathOf("package"));
        WriteContent(package);
        WriteFile(package / "bagi-package.txt", "Game\n");

        const auto roots = FindPackagedContentRoots(package);

        ASSERT_TRUE(roots.has_value());
        FileSystem files;
        files.AddContentRoot(roots->engine);
        files.AddContentRoot(roots->game);
        files.Initialize();
        const Vector<Path> layers = files.GetMountLayers(MountPoints::config);
        ASSERT_EQ(layers.size(), 2u);
        EXPECT_EQ(layers[0], AbsoluteNormalPath(package / "content/engine/config"));
        EXPECT_EQ(layers[1], AbsoluteNormalPath(package / "content/game/config"));
    }

    TEST(PackagedContentTest, EmptyFolderWithoutMarkerGivesNothing) {
        const TestTempFolder folder("PackagedContentEmpty");

        EXPECT_FALSE(FindPackagedContentRoots(Path(folder.PathOf("package"))).has_value());
    }

    TEST(PackagedContentTest, ContentFolderWithoutMarkerGivesNothing) {
        const TestTempFolder folder("PackagedContentUnmarked");
        const Path package(folder.PathOf("package"));
        WriteContent(package);

        EXPECT_FALSE(FindPackagedContentRoots(package).has_value());
    }

    TEST(PackagedContentTest, UnknownExecutableFolderIgnoresAMarkerInTheWorkingFolder) {
        const TestTempFolder folder("PackagedContentNoFolder");
        const Path package(folder.PathOf("package"));
        WriteFile(package / "bagi-package.txt", "Game\n");
        const ScopedCurrentDirectory workingFolder(package);

        EXPECT_FALSE(FindPackagedContentRoots(Path()).has_value());
    }

}  // namespace BECore::Tests
