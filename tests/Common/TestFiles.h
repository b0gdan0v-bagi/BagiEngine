#pragma once

#include <BECore/File/File.h>
#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <Common/NativePath.h>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <system_error>

namespace BECore::Tests {

    /// Writes @p text to @p path byte for byte (no line-end translation), making the folders above it. The file is rewritten in place
    /// (opened and truncated, never replaced by a rename), because the file-watch tests need the edit of an existing file to be seen as one.
    /// A step that fails fails the test with the path, so a later check does not fail for a reason it cannot name
    inline void WriteFile(const Path& path, StringView text) {
        if (const Path folder = path.Parent(); !folder.Empty()) {
            const auto made = CreateDirectories(folder);
            if (!made) {
                ADD_FAILURE() << "could not make the folder of " << path.CStr() << ": " << made.error().c_str();
                return;
            }
        }
        auto file = File::Open(path, FileMode::Write);
        if (!file) {
            ADD_FAILURE() << "could not open " << path.CStr() << ": " << file.error().c_str();
            return;
        }
        const auto written = file->Write(text);
        if (!written) {
            ADD_FAILURE() << "could not write " << path.CStr() << ": " << written.error().c_str();
            return;
        }
        const auto closed = file->Close();
        EXPECT_TRUE(closed.has_value()) << "could not close " << path.CStr() << ": " << (closed ? "" : closed.error().c_str());
    }

    /// The bytes of @p path; a file that cannot be read fails the test and gives an empty result, so a later check on it does not pass on nothing
    inline Vector<uint8_t> ReadFileBytes(const Path& path) {
        auto bytes = BECore::ReadFileBytes(path);
        if (!bytes) {
            ADD_FAILURE() << "could not read " << path.CStr() << ": " << bytes.error().c_str();
            return {};
        }
        return std::move(*bytes);
    }

    /// The content of @p path byte for byte (no line-end translation); failures as ReadFileBytes
    inline String ReadFile(const Path& path) {
        const Vector<uint8_t> bytes = ReadFileBytes(path);
        return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    /// Makes @p path and the folders above it; a folder that cannot be made fails the test
    inline void MakeDirectory(const Path& path) {
        const auto made = CreateDirectories(path);
        EXPECT_TRUE(made.has_value()) << "could not make " << path.CStr() << ": " << (made ? "" : made.error().c_str());
    }

    /// Removes @p path and everything below it; a failure only leaves a temporary folder behind, so it is not a test failure
    inline void RemoveDirectory(const Path& path) {
        [[maybe_unused]] const auto ignored = RemoveTree(path);
    }

    /// RemoveDirectory, then MakeDirectory: an empty folder whatever an earlier run left there
    inline void MakeEmptyDirectory(const Path& path) {
        RemoveDirectory(path);
        MakeDirectory(path);
    }

    /// Whether @p path exists, following a symbolic link (a link that points nowhere does not)
    [[nodiscard]] inline bool PathExists(const Path& path) {
        return GetFileInfo(path).has_value();
    }

    [[nodiscard]] inline bool IsFile(const Path& path) {
        const Optional<FileInfo> info = GetFileInfo(path);
        return info && info->kind == FileKind::File;
    }

    [[nodiscard]] inline bool IsFolder(const Path& path) {
        const Optional<FileInfo> info = GetFileInfo(path);
        return info && info->kind == FileKind::Directory;
    }

    /// Removes the file, or the symbolic link to a file, @p path (a link to a folder is RemoveDirectory's, as RemoveFile refuses it); one that
    /// is not there is no failure, any other error fails the test
    inline void RemoveFileIfPresent(const Path& path) {
        if (!PathExists(path) && !IsSymbolicLink(path)) {
            return;
        }
        const auto removed = RemoveFile(path);
        EXPECT_TRUE(removed.has_value()) << "could not remove " << path.CStr() << ": " << (removed ? "" : removed.error().c_str());
    }

    // ---- the one std::filesystem boundary of the tests (what the file layer has no call for)

    /// The system temp folder: the file layer knows no folder of the OS
    inline Path SystemTempDirectory() {
        Path folder = SystemTempFolder();
        EXPECT_FALSE(folder.Empty()) << "no system temp folder";
        return folder;
    }

    /// Sets the write time of @p path to now plus @p fromNow (a negative value is the past), on the clock `std::filesystem` reads, so a
    /// watcher that compares times sees a change whatever the resolution of the file system. The layer reads times but sets none. A test
    /// compares `GetFileInfo(path)->modifyTimeNs` before and after an operation, never the number it set here: the clocks differ in resolution
    inline void SetWriteTime(const Path& path, std::chrono::nanoseconds fromNow) {
        using Clock = std::filesystem::file_time_type::clock;                                                 //-NoStd: the one std::filesystem boundary of the tests
        const std::filesystem::path native = NativePath(path);                                                //-NoStd: the one std::filesystem boundary of the tests
        const auto when = Clock::now() + std::chrono::duration_cast<Clock::duration>(fromNow);                //-NoStd: the one std::filesystem boundary of the tests
        std::error_code error;                                                                                //-NoStd: the one std::filesystem boundary of the tests
        std::filesystem::last_write_time(native, when, error);                                                //-NoStd: the one std::filesystem boundary of the tests
        EXPECT_FALSE(error) << "could not set the write time of " << path.CStr() << ": " << error.message();  //-NoStd: the one std::filesystem boundary of the tests
    }

    /// Makes the symbolic link @p link to @p target, a folder link when @p target is a folder; the layer reads links but makes none
    inline void CreateSymlink(const Path& link, const Path& target) {
        const std::filesystem::path nativeLink = NativePath(link);      //-NoStd: the one std::filesystem boundary of the tests
        const std::filesystem::path nativeTarget = NativePath(target);  //-NoStd: the one std::filesystem boundary of the tests
        std::error_code error;                                          //-NoStd: the one std::filesystem boundary of the tests
        if (IsFolder(target)) {
            std::filesystem::create_directory_symlink(nativeTarget, nativeLink, error);  //-NoStd: the one std::filesystem boundary of the tests
        } else {
            std::filesystem::create_symlink(nativeTarget, nativeLink, error);  //-NoStd: the one std::filesystem boundary of the tests
        }
        EXPECT_FALSE(error) << "could not link " << link.CStr() << " to " << target.CStr() << ": " << error.message();  //-NoStd: the one std::filesystem boundary of the tests
    }

    /// Sets the size of the file @p path to @p size without writing the bytes (sparse), for a limit check on a file nobody could afford to write
    inline void ResizeFile(const Path& path, uint64_t size) {
        const std::filesystem::path native = NativePath(path);                                 //-NoStd: the one std::filesystem boundary of the tests
        std::error_code error;                                                                 //-NoStd: the one std::filesystem boundary of the tests
        std::filesystem::resize_file(native, size, error);                                     //-NoStd: the one std::filesystem boundary of the tests
        EXPECT_FALSE(error) << "could not resize " << path.CStr() << ": " << error.message();  //-NoStd: the one std::filesystem boundary of the tests
    }

    /// Sets the working folder of the process to @p folder and puts the old one back in the destructor. The layer reads the working folder
    /// (`CurrentDirectory`) and never sets it. The tests of one executable run one at a time in a process, so no other case sees the change
    class ScopedCurrentDirectory {
    public:
        explicit ScopedCurrentDirectory(const Path& folder) {
            std::error_code error;                                                                  //-NoStd: the one std::filesystem boundary of the tests
            _previous = std::filesystem::current_path(error);                                       //-NoStd: the one std::filesystem boundary of the tests
            EXPECT_FALSE(error) << "no working folder: " << error.message();                        //-NoStd: the one std::filesystem boundary of the tests
            const std::filesystem::path native = NativePath(folder);                                //-NoStd: the one std::filesystem boundary of the tests
            std::filesystem::current_path(native, error);                                           //-NoStd: the one std::filesystem boundary of the tests
            EXPECT_FALSE(error) << "could not enter " << folder.CStr() << ": " << error.message();  //-NoStd: the one std::filesystem boundary of the tests
        }
        ~ScopedCurrentDirectory() {
            std::error_code error;                                                                 //-NoStd: the one std::filesystem boundary of the tests
            std::filesystem::current_path(_previous, error);                                       //-NoStd: the one std::filesystem boundary of the tests
            EXPECT_FALSE(error) << "could not return to the working folder: " << error.message();  //-NoStd: the one std::filesystem boundary of the tests
        }
        ScopedCurrentDirectory(const ScopedCurrentDirectory&) = delete;
        ScopedCurrentDirectory& operator=(const ScopedCurrentDirectory&) = delete;

    private:
        std::filesystem::path _previous;  //-NoStd: the one std::filesystem boundary of the tests
    };

    /// What `std::filesystem` says, as the oracle the file layer's `CurrentDirectory`, `CanonicalPath` and `WeaklyCanonicalPath` are compared with
    /// (generic text). A failure fails the test and gives an empty text
    inline String OsCurrentDirectory() {
        std::error_code error;                                                      //-NoStd: the one std::filesystem boundary of the tests
        const std::filesystem::path folder = std::filesystem::current_path(error);  //-NoStd: the one std::filesystem boundary of the tests
        EXPECT_FALSE(error) << "no working folder: " << error.message();            //-NoStd: the one std::filesystem boundary of the tests
        return FromStdString(folder.generic_string());                              //-NoStd: the one std::filesystem boundary of the tests
    }

    inline String OsCanonical(const Path& path) {
        const std::filesystem::path native = NativePath(path);                                     //-NoStd: the one std::filesystem boundary of the tests
        std::error_code error;                                                                     //-NoStd: the one std::filesystem boundary of the tests
        const std::filesystem::path result = std::filesystem::canonical(native, error);            //-NoStd: the one std::filesystem boundary of the tests
        EXPECT_FALSE(error) << "no canonical form of " << path.CStr() << ": " << error.message();  //-NoStd: the one std::filesystem boundary of the tests
        return FromStdString(result.generic_string());                                             //-NoStd: the one std::filesystem boundary of the tests
    }

    inline String OsWeaklyCanonical(const Path& path) {
        const std::filesystem::path native = NativePath(path);                                            //-NoStd: the one std::filesystem boundary of the tests
        std::error_code error;                                                                            //-NoStd: the one std::filesystem boundary of the tests
        const std::filesystem::path result = std::filesystem::weakly_canonical(native, error);            //-NoStd: the one std::filesystem boundary of the tests
        EXPECT_FALSE(error) << "no weakly canonical form of " << path.CStr() << ": " << error.message();  //-NoStd: the one std::filesystem boundary of the tests
        return FromStdString(result.generic_string());                                                    //-NoStd: the one std::filesystem boundary of the tests
    }

    // ---- end of the std::filesystem boundary

}  // namespace BECore::Tests
