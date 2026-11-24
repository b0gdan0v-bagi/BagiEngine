#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <filesystem>    //-NoStd: the one std::filesystem boundary of the tests
#include <string_view>   //-NoStd: the one std::filesystem boundary of the tests
#include <system_error>  //-NoStd: the one std::filesystem boundary of the tests

namespace BECore::Tests {

    /// The conversions every `std::filesystem` line of the tests and the fuzz targets goes through (no gtest here: the fuzz targets do not link it).
    /// UTF-8 both ways, because `path::string()` is the ANSI code page on Windows
    inline std::filesystem::path NativePath(const Path& path) {  //-NoStd: the one std::filesystem boundary of the tests
        const StringView text = path.View();
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));  //-NoStd: the one std::filesystem boundary of the tests
    }

    inline Path FromNativePath(const std::filesystem::path& path) {  //-NoStd: the one std::filesystem boundary of the tests
        const auto utf8 = path.u8string();                           //-NoStd: the one std::filesystem boundary of the tests
        return Path(StringView(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
    }

    /// The system temp folder, or an empty Path when the OS has none
    inline Path SystemTempFolder() {
        std::error_code error;                                                             //-NoStd: the one std::filesystem boundary of the tests
        const std::filesystem::path folder = std::filesystem::temp_directory_path(error);  //-NoStd: the one std::filesystem boundary of the tests
        return error ? Path() : FromNativePath(folder);
    }

}  // namespace BECore::Tests
