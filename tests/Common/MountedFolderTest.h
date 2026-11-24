#pragma once

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Format/Format.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/PoolString/PoolString.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /**
     * A fixture that mounts an empty temporary folder as the virtual path given to its constructor, for the cases that read
     * files through the FileSystem. The folder is one per test and per process: ctest runs the tests of one executable in
     * parallel processes, and two checkouts may run the same test at the same moment (TempPath.h). The mount point must be a
     * string of static lifetime.
     */
    class MountedFolderTest : public ::testing::Test {
    protected:
        explicit MountedFolderTest(const char* mountPoint) : _mountPoint(mountPoint) {}

        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory(_mountPoint);
            MakeEmptyDirectory(_dir);
            ASSERT_TRUE(CoreManager::GetFileSystem().Mount(PoolString::Intern(_mountPoint), _dir));
        }

        void TearDown() override {
            CoreManager::GetFileSystem().Unmount(PoolString::Intern(_mountPoint));
            RemoveDirectory(_dir);
        }

        /// The path of @p fileName of the folder as the FileSystem sees it
        [[nodiscard]] String VirtualPath(const char* fileName) const {
            return Format("{}/{}", _mountPoint, fileName);
        }

        Path _dir;

    private:
        const char* _mountPoint;
    };

}  // namespace BECore::Tests
