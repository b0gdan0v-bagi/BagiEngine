#pragma once

#include <BECore/Types/Types.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <system_error>

namespace BECore::Tests {

    /// Writes @p text to @p path byte for byte (no line-end translation), making the folders above it; a file that cannot be written fails the
    /// test, so a later check does not fail for a reason it cannot name
    inline void WriteFile(const std::filesystem::path& path, StringView text) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);  //-NoStd: a test writes its own temporary files, outside the engine's file layer
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        EXPECT_TRUE(file.good()) << "could not write " << path.string();
    }

    /// The bytes of @p path; a file that cannot be read fails the test and gives an empty result, so a later check on it does not pass on nothing
    inline Vector<uint8_t> ReadFileBytes(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);  //-NoStd: a test reads its own temporary files, outside the engine's file layer
        if (!file.good()) {
            ADD_FAILURE() << "could not read " << path.string();
            return {};
        }
        Vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        EXPECT_TRUE(file.good()) << "could not read " << path.string();
        return bytes;
    }

    /// The content of @p path byte for byte (no line-end translation); failures as ReadFileBytes
    inline String ReadFile(const std::filesystem::path& path) {
        const Vector<uint8_t> bytes = ReadFileBytes(path);
        return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    /// Makes @p path and the folders above it; a folder that cannot be made fails the test. The error_code overload: the
    /// build has no exceptions, so the throwing one would abort the whole process on a failure
    inline void MakeDirectory(const std::filesystem::path& path) {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        EXPECT_FALSE(error) << "could not make " << path.string() << ": " << error.message();
    }

    /// Removes @p path and everything below it; a failure only leaves a temporary folder behind, so it is not a test failure
    inline void RemoveDirectory(const std::filesystem::path& path) {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    /// RemoveDirectory, then MakeDirectory: an empty folder whatever an earlier run left there
    inline void MakeEmptyDirectory(const std::filesystem::path& path) {
        RemoveDirectory(path);
        MakeDirectory(path);
    }

}  // namespace BECore::Tests
