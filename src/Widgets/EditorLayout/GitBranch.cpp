#include "GitBranch.h"

#include <BECore/File/File.h>

namespace BECore {

    namespace {

        constexpr StringView kBranchRefPrefix = "ref: refs/heads/";
        constexpr StringView kGitDirPrefix = "gitdir:";
        constexpr StringView kGitEntryName = ".git";
        constexpr StringView kHeadFileName = "HEAD";
        constexpr StringView kBlank = " \t\r\n";
        constexpr size_t kSha1HexLength = 40;
        constexpr size_t kSha256HexLength = 64;

        StringView Trim(StringView text) {
            const size_t first = text.find_first_not_of(kBlank);
            if (first == StringView::npos) {
                return {};
            }
            return text.substr(first, text.find_last_not_of(kBlank) - first + 1);
        }

        bool IsHexId(StringView text) {
            if (text.size() != kSha1HexLength && text.size() != kSha256HexLength) {
                return false;
            }
            return std::ranges::all_of(text, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
        }

        /// The folder with HEAD for the `.git` entry @p gitEntry of @p repositoryRoot; empty when it is neither a folder nor a gitdir file
        Path ResolveGitDirectory(const Path& gitEntry, const Path& repositoryRoot) {
            const Optional<FileInfo> info = GetFileInfo(gitEntry);
            if (info && info->kind == FileKind::Directory) {
                return gitEntry;
            }
            if (!info || info->kind != FileKind::File) {
                return {};
            }
            const auto content = ReadTextFile(gitEntry);
            if (!content) {
                return {};
            }
            const StringView line = Trim(*content);
            if (!line.starts_with(kGitDirPrefix)) {
                return {};
            }
            const StringView target = Trim(line.substr(kGitDirPrefix.size()));
            if (target.empty()) {
                return {};
            }
            const Path targetPath(target);
            return targetPath.IsAbsolute() ? targetPath : (repositoryRoot / targetPath).LexicallyNormal();
        }

    }  // namespace

    String ParseGitHead(StringView text) {
        const StringView line = Trim(text);
        if (line.starts_with(kBranchRefPrefix)) {
            const StringView name = line.substr(kBranchRefPrefix.size());
            return String(name.data(), name.size());
        }
        if (IsHexId(line)) {
            return String(line.data(), kShortCommitLength);
        }
        return {};
    }

    String ReadGitBranch(const Path& startDirectory) {
        // AbsolutePath("") is the working folder, which is not what an empty start means
        if (startDirectory.Empty()) {
            return {};
        }
        std::expected<String, String> absolute = AbsolutePath(startDirectory);
        if (!absolute) {
            return {};
        }
        Path directory(std::move(*absolute));
        while (true) {
            const Path gitEntry = directory / kGitEntryName;
            if (GetFileInfo(gitEntry)) {
                const Path gitDirectory = ResolveGitDirectory(gitEntry, directory);
                if (gitDirectory.Empty()) {
                    return {};
                }
                const auto head = ReadTextFile(gitDirectory / kHeadFileName);
                return head ? ParseGitHead(*head) : String();
            }
            const Path parent = directory.Parent();
            if (parent.Empty() || parent == directory) {
                return {};
            }
            directory = parent;
        }
    }

}  // namespace BECore
