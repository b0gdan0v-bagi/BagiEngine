#include "ArtGenSetStore.h"

#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/EditorLayout/UserFileIo.h>
#include <system_error>

namespace BECore {

    namespace {

        std::filesystem::path PathOf(StringView text) {
            return std::filesystem::path(ToStd(text));  //-NoStd: std::filesystem::path takes a std::string_view
        }

        std::filesystem::path OutRoot(const std::filesystem::path& gameRoot) {
            return gameRoot / PathOf(kArtGenOutFolder);
        }

        /// A `\` of a Windows path separates folders on every platform here, so a manifest written on one machine reads on another
        String WithForwardSlashes(StringView text) {
            String result(text);
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
        }

        String Generic(const std::filesystem::path& path) {
            const std::string text = path.generic_string();  //-NoStd: the text of a std::filesystem::path
            return String(text.c_str(), text.size());
        }

        void CancelInterrupted(ArtGenSet& set) {
            for (ArtGenSetJob& job : set.jobs) {
                if (job.state == ArtGenSetJobState::Pending || job.state == ArtGenSetJobState::Running) {
                    EndArtGenJob(job, ArtGenSetJobState::Cancelled, kEditorClosedReason);
                }
            }
        }

        /// The set of one folder, or why it is skipped
        std::expected<ArtGenSet, String> LoadSet(const std::filesystem::path& folder) {
            const String name = FromStdString(folder.filename().string());  //-NoStd: the text of a std::filesystem::path
            if (!IsValidSpriteName(name)) {
                return std::unexpected(Format("'{}' is not a valid set name", name));
            }
            const auto text = ReadTextFile(folder / PathOf(kArtGenManifestFile));
            if (!text) {
                return std::unexpected(text.error());
            }
            auto parsed = ParseJson(*text);
            if (!parsed) {
                return std::unexpected(Format("not JSON: {}", parsed.error().message));
            }
            auto set = ArtGenSetFromJson(*parsed);
            if (!set) {
                return std::unexpected(set.error());
            }
            set->name = name;
            CancelInterrupted(*set);
            return set;
        }

    }  // namespace

    std::filesystem::path SetFolder(const std::filesystem::path& gameRoot, StringView set) {
        return OutRoot(gameRoot) / PathOf(set);
    }

    std::filesystem::path ManifestPath(const std::filesystem::path& gameRoot, StringView set) {
        return SetFolder(gameRoot, set) / PathOf(kArtGenManifestFile);
    }

    std::filesystem::path JobFolder(const std::filesystem::path& gameRoot, StringView set, StringView job) {
        return SetFolder(gameRoot, set) / PathOf(job);
    }

    String OutDirOf(StringView set, StringView job) {
        return Format("{}/{}", set, job);
    }

    bool NewSetFolderFree(const std::filesystem::path& gameRoot, StringView name) {
        if (!IsValidSpriteName(name)) {
            return false;
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(SetFolder(gameRoot, name), error);
        return !exists && !error;
    }

    std::expected<void, String> WriteManifest(const std::filesystem::path& gameRoot, const ArtGenSet& set) {
        if (!IsValidSpriteName(set.name)) {
            return std::unexpected(Format("'{}' is not a valid set name", set.name));
        }
        String text = DumpJson(ArtGenSetToJson(set));
        text += '\n';
        return WriteTextFileAtomically(ManifestPath(gameRoot, set.name), text);
    }

    Vector<ArtGenSet> LoadSets(const std::filesystem::path& gameRoot) {
        Vector<ArtGenSet> sets;
        std::error_code error;
        //-NoStd: one listing of the game's sets at start, no engine file layer lists folders
        std::filesystem::directory_iterator it(OutRoot(gameRoot), error);
        if (error) {
            return sets;
        }
        for (const std::filesystem::directory_entry& entry : it) {
            std::error_code entryError;
            if (!entry.is_directory(entryError)) {
                continue;
            }
            const std::filesystem::path manifest = entry.path() / PathOf(kArtGenManifestFile);
            if (!std::filesystem::exists(manifest, entryError)) {
                continue;
            }
            auto set = LoadSet(entry.path());
            if (!set) {
                LOG_WARNING("Art generation: the set '{}' is skipped and left as it is: {}", Generic(entry.path()), set.error());
                continue;
            }
            sets.push_back(std::move(*set));
        }
        std::sort(sets.begin(), sets.end(), [](const ArtGenSet& left, const ArtGenSet& right) { return left.created != right.created ? left.created < right.created : left.name < right.name; });
        return sets;
    }

    String RelativeToSet(const std::filesystem::path& setFolder, StringView realPath) {
        const std::filesystem::path given = PathOf(WithForwardSlashes(realPath));
        const std::filesystem::path relative = given.lexically_normal().lexically_relative(setFolder.lexically_normal());
        if (relative.empty() || *relative.begin() == "..") {
            return Generic(given);
        }
        return Generic(relative);
    }

    std::filesystem::path AbsoluteInSet(const std::filesystem::path& gameRoot, StringView set, StringView file) {
        if (file.empty()) {
            return {};
        }
        const std::filesystem::path path = PathOf(WithForwardSlashes(file));
        if (path.has_root_name() || path.has_root_directory()) {
            return path;
        }
        for (const std::filesystem::path& part : path) {
            if (part == "..") {
                return {};
            }
        }
        return SetFolder(gameRoot, set) / path;
    }

}  // namespace BECore
