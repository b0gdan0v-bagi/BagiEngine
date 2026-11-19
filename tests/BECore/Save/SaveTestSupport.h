#pragma once

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Format/Format.h>
#include <BECore/Types/Types.h>
#include <Common/TempPath.h>
#include <filesystem>
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
            _base = TestTempDirectory(Format("Bagi{}", ::testing::UnitTest::GetInstance()->current_test_info()->test_suite_name()));
            std::error_code ec;
            std::filesystem::remove_all(_base, ec);
            std::filesystem::create_directories(_base, ec);
            ASSERT_FALSE(ec) << "cannot make " << _base.string() << ": " << ec.message();
            _saves = _base / "saves";
        }

        void TearDown() override {
            std::error_code ec;
            std::filesystem::remove_all(_base, ec);
        }

        std::filesystem::path _base;
        std::filesystem::path _saves;
        FileSystem _fileSystem;
    };

}  // namespace BECore::Tests
