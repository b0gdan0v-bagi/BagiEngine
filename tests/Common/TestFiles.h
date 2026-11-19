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

}  // namespace BECore::Tests
