#pragma once

#include <BECore/File/Path.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Format/Format.h>
#include <BECore/Types/Types.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>
#include <initializer_list>

namespace BECore::Tests {

    inline Vector<uint8_t> Bytes(std::initializer_list<uint8_t> values) {
        return Vector<uint8_t>(values);
    }

    /// An empty temp folder per test, named after the suite and the case, and a local FileSystem: no global services, so the
    /// tests run beside each other. A fixture that adds to SetUp() calls this one first under ASSERT_NO_FATAL_FAILURE.
    class SaveFolderTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _base = Path(TestTempDirectory(Format("Bagi{}", ::testing::UnitTest::GetInstance()->current_test_info()->test_suite_name())));
            MakeEmptyDirectory(_base);
            ASSERT_TRUE(IsFolder(_base)) << "cannot make " << _base.CStr();
            _saves = _base / "saves";
        }

        void TearDown() override {
            RemoveDirectory(_base);
        }

        Path _base;
        Path _saves;
        FileSystem _fileSystem;
    };

}  // namespace BECore::Tests
