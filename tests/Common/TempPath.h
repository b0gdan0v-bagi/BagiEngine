#pragma once

#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <string>

namespace BECore::Tests {

    /**
     * A name in the system temp folder that only this process uses: "<stem>_<tag>", the tag drawn once per process.
     * ctest -j runs the tests of one executable in parallel processes, and two checkouts (the parallel todo-loop
     * workers) may run the same tests at the same moment, so a fixed name, or one made of the test name alone, can be
     * cleared or overwritten by another process. The caller removes what it made (a crashed run leaves its folder).
     */
    inline std::filesystem::path TestTempPath(const std::string& stem) {
        static const std::string tag = [] {
            constexpr size_t kTagDigits = 8;
            std::random_device device;
            const uint32_t value = device();
            std::string text;
            for (size_t digit = 0; digit < kTagDigits; ++digit) {
                text.insert(text.begin(), "0123456789abcdef"[(value >> (digit * 4)) & 0xF]);
            }
            return text;
        }();
        return std::filesystem::temp_directory_path() / (stem + "_" + tag);
    }

    /// TestTempPath("<prefix>_<name of the running test>"): one folder per test and per process
    inline std::filesystem::path TestTempDirectory(const std::string& prefix) {
        return TestTempPath(prefix + "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    }

}  // namespace BECore::Tests
