#pragma once

namespace BECore {

    /// Suffix of the temporary file next to the one being written
    inline constexpr eastl::string_view kTempFileSuffix = ".tmp";

    /// Reads the whole file as text. The error is a sentence for the log (missing file, not a file, read failure).
    [[nodiscard]] std::expected<eastl::string, eastl::string> ReadTextFile(const std::filesystem::path& path);

    /**
     * @brief Writes @p text to @p path so that a reader sees the old file or the new one, never a half-written file.
     *
     * Writes `<path>.tmp` and renames it over @p path (std::filesystem::rename replaces an existing file), as
     * SaveGameService does. The folder of @p path is made when it is missing. On any failure the old file stays and the
     * temporary file is removed; the error is a sentence for the log.
     */
    [[nodiscard]] std::expected<void, eastl::string> WriteTextFileAtomically(const std::filesystem::path& path, eastl::string_view text);

}  // namespace BECore
