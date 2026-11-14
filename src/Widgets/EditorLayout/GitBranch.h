#pragma once

namespace BECore {

    /// Length of the short form of a commit id shown for a detached HEAD
    inline constexpr size_t kShortCommitLength = 7;

    /**
     * @brief The branch named by the text of a git `HEAD` file.
     *
     * `ref: refs/heads/<name>` gives `<name>` (it may hold slashes); a line of 40 (SHA-1) or 64 (SHA-256) hex digits, a detached
     * HEAD, gives its first kShortCommitLength characters; a trailing newline, CR and spaces around the line are ignored. Anything
     * else (garbage, an empty text, a `ref:` to something that is not a branch) gives an empty string.
     */
    [[nodiscard]] eastl::string ParseGitHead(StringView text);

    /**
     * @brief The branch (or the short commit id) of the repository that contains @p startDirectory; empty when there is none.
     *
     * Walks up from @p startDirectory to the first directory that has `.git` and reads its `HEAD`. A `.git` that is a file
     * (`gitdir: <path>`, the shape in a submodule or a worktree; a relative path is taken from the folder of that file) is followed.
     * Reads only `HEAD`, never throws; a shipped build has no repository above its content and gets an empty string.
     */
    [[nodiscard]] eastl::string ReadGitBranch(const std::filesystem::path& startDirectory);

}  // namespace BECore
