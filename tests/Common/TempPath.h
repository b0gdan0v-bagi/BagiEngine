#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Types/Types.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>

namespace BECore::Tests {

    /**
     * A name in the system temp folder that only this process uses: "<stem>_<tag>", the tag drawn once per process.
     * ctest -j runs the tests of one executable in parallel processes, and two checkouts (the parallel todo-loop
     * workers) may run the same tests at the same moment, so a fixed name, or one made of the test name alone, can be
     * cleared or overwritten by another process. The caller removes what it made (a crashed run leaves its folder).
     */
    inline std::filesystem::path TestTempPath(const String& stem) {
        static const String tag = Format("{:08x}", std::random_device{}());
        const String name = Format("{}_{}", stem, tag);
        return std::filesystem::temp_directory_path() / ToStd(name);
    }

    /// TestTempPath("<prefix>_<name of the running test>"): one folder per test and per process
    inline std::filesystem::path TestTempDirectory(const String& prefix) {
        return TestTempPath(prefix + "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    }

    /// The folder TestTempDirectory(prefix), cleared and created by the constructor and removed with the object
    class TestTempFolder {
    public:
        explicit TestTempFolder(const String& prefix) : _path(TestTempDirectory(prefix)) {
            std::error_code error;
            std::filesystem::remove_all(_path, error);
            std::filesystem::create_directories(_path, error);
            EXPECT_FALSE(error) << "cannot create " << _path.generic_string() << ": " << error.message();
        }
        ~TestTempFolder() {
            std::error_code ignored;
            std::filesystem::remove_all(_path, ignored);
        }
        TestTempFolder(const TestTempFolder&) = delete;
        TestTempFolder& operator=(const TestTempFolder&) = delete;

        /// The absolute path of @p name in the folder; nothing is written
        String PathOf(const char* name) const {
            return FromStdString((_path / name).generic_string());
        }

        /// Writes a file and returns its absolute path, which the file system takes as it is
        String Write(const char* name, const String& content) const {
            std::ofstream(_path / name).write(content.data(), static_cast<std::streamsize>(content.size()));
            return PathOf(name);
        }

    private:
        std::filesystem::path _path;
    };

}  // namespace BECore::Tests
