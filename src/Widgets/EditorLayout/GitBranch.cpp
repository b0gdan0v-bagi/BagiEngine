#include "GitBranch.h"

#include "UserFileIo.h"

namespace BECore {

    namespace {

        constexpr eastl::string_view kBranchRefPrefix = "ref: refs/heads/";
        constexpr eastl::string_view kGitDirPrefix = "gitdir:";
        constexpr eastl::string_view kGitEntryName = ".git";
        constexpr eastl::string_view kHeadFileName = "HEAD";
        constexpr eastl::string_view kBlank = " \t\r\n";
        constexpr size_t kSha1HexLength = 40;
        constexpr size_t kSha256HexLength = 64;

        eastl::string_view Trim(eastl::string_view text) {
            const size_t first = text.find_first_not_of(kBlank);
            if (first == eastl::string_view::npos) {
                return {};
            }
            return text.substr(first, text.find_last_not_of(kBlank) - first + 1);
        }

        bool IsHexId(eastl::string_view text) {
            if (text.size() != kSha1HexLength && text.size() != kSha256HexLength) {
                return false;
            }
            return std::ranges::all_of(text, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
        }

        std::filesystem::path ToPath(eastl::string_view text) {
            return std::filesystem::path(std::string(text.data(), text.size()));
        }

        /// The folder with HEAD for the `.git` entry @p gitEntry of @p repositoryRoot; empty when it is neither a folder nor a gitdir file
        std::filesystem::path ResolveGitDirectory(const std::filesystem::path& gitEntry, const std::filesystem::path& repositoryRoot) {
            std::error_code ec;
            if (std::filesystem::is_directory(gitEntry, ec)) {
                return gitEntry;
            }
            if (!std::filesystem::is_regular_file(gitEntry, ec)) {
                return {};
            }
            const auto content = ReadTextFile(gitEntry);
            if (!content) {
                return {};
            }
            const eastl::string_view line = Trim(*content);
            if (!line.starts_with(kGitDirPrefix)) {
                return {};
            }
            const eastl::string_view target = Trim(line.substr(kGitDirPrefix.size()));
            if (target.empty()) {
                return {};
            }
            const std::filesystem::path targetPath = ToPath(target);
            return targetPath.is_absolute() ? targetPath : (repositoryRoot / targetPath).lexically_normal();
        }

    }  // namespace

    eastl::string ParseGitHead(eastl::string_view text) {
        const eastl::string_view line = Trim(text);
        if (line.starts_with(kBranchRefPrefix)) {
            const eastl::string_view name = line.substr(kBranchRefPrefix.size());
            return eastl::string(name.data(), name.size());
        }
        if (IsHexId(line)) {
            return eastl::string(line.data(), kShortCommitLength);
        }
        return {};
    }

    eastl::string ReadGitBranch(const std::filesystem::path& startDirectory) {
        std::error_code ec;
        std::filesystem::path directory = std::filesystem::absolute(startDirectory, ec).lexically_normal();
        if (ec) {
            return {};
        }
        while (true) {
            const std::filesystem::path gitEntry = directory / std::string(kGitEntryName.data(), kGitEntryName.size());
            if (std::filesystem::exists(gitEntry, ec)) {
                const std::filesystem::path gitDirectory = ResolveGitDirectory(gitEntry, directory);
                if (gitDirectory.empty()) {
                    return {};
                }
                const auto head = ReadTextFile(gitDirectory / std::string(kHeadFileName.data(), kHeadFileName.size()));
                return head ? ParseGitHead(*head) : eastl::string();
            }
            const std::filesystem::path parent = directory.parent_path();
            if (parent.empty() || parent == directory) {
                return {};
            }
            directory = parent;
        }
    }

}  // namespace BECore
