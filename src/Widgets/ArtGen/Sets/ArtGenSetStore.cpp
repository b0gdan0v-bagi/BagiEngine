#include "ArtGenSetStore.h"

#include <BECore/File/File.h>
#include <Widgets/ArtGen/ArtGenAccept.h>

namespace BECore {

    namespace {

        Path OutRoot(const Path& gameRoot) {
            return gameRoot / kArtGenOutFolder;
        }

        /// A `\` of a Windows path separates folders on every platform here, so a manifest written on one machine reads on another
        String WithForwardSlashes(StringView text) {
            String result(text);
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
        }

        void CancelInterrupted(ArtGenSet& set) {
            for (ArtGenSetJob& job : set.jobs) {
                if (job.state == ArtGenSetJobState::Pending || job.state == ArtGenSetJobState::Running) {
                    EndArtGenJob(job, ArtGenSetJobState::Cancelled, kEditorClosedReason);
                }
            }
        }

        /// The set of one folder, or why it is skipped
        std::expected<ArtGenSet, String> LoadSet(const Path& folder) {
            const String name(folder.Filename().View());
            if (!IsValidSpriteName(name)) {
                return std::unexpected(Format("'{}' is not a valid set name", name));
            }
            const auto text = ReadTextFile(folder / kArtGenManifestFile);
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

    Path SetFolder(const Path& gameRoot, StringView set) {
        return OutRoot(gameRoot) / set;
    }

    Path ManifestPath(const Path& gameRoot, StringView set) {
        return SetFolder(gameRoot, set) / kArtGenManifestFile;
    }

    Path JobFolder(const Path& gameRoot, StringView set, StringView job) {
        return SetFolder(gameRoot, set) / job;
    }

    String OutDirOf(StringView set, StringView job) {
        return Format("{}/{}", set, job);
    }

    bool NewSetFolderFree(const Path& gameRoot, StringView name) {
        return IsValidSpriteName(name) && !GetFileInfo(SetFolder(gameRoot, name));
    }

    std::expected<void, String> WriteManifest(const Path& gameRoot, const ArtGenSet& set) {
        if (!IsValidSpriteName(set.name)) {
            return std::unexpected(Format("'{}' is not a valid set name", set.name));
        }
        String text = DumpJson(ArtGenSetToJson(set));
        text += '\n';
        return WriteFileAtomically(ManifestPath(gameRoot, set.name), text);
    }

    Vector<ArtGenSet> LoadSets(const Path& gameRoot) {
        Vector<ArtGenSet> sets;
        const Path outRoot = OutRoot(gameRoot);
        Vector<String> folders;
        const auto listed = WalkDirectory(outRoot, WalkMode::Folder, [&folders](StringView name, FileKind kind) {
            if (kind == FileKind::Directory) {
                folders.emplace_back(name);
            }
            return true;
        });
        if (!listed) {
            return sets;
        }
        for (const String& name : folders) {
            const Path folder = outRoot / name;
            const auto manifest = GetFileInfo(folder / kArtGenManifestFile);
            if (!manifest || manifest->kind != FileKind::File) {
                continue;
            }
            auto set = LoadSet(folder);
            if (!set) {
                LOG_WARNING("Art generation: the set '{}' is skipped and left as it is: {}", folder.View(), set.error());
                continue;
            }
            sets.push_back(std::move(*set));
        }
        std::sort(sets.begin(), sets.end(), [](const ArtGenSet& left, const ArtGenSet& right) { return left.created != right.created ? left.created < right.created : left.name < right.name; });
        return sets;
    }

    String RelativeToSet(const Path& setFolder, StringView realPath) {
        const Path given(WithForwardSlashes(realPath));
        // Relative is empty for a path that is not below the folder (also one that climbs out with `..`) and for the folder itself
        const Path relative = given.LexicallyNormal().Relative(setFolder.LexicallyNormal());
        return String(relative.Empty() ? given.View() : relative.View());
    }

    Path AbsoluteInSet(const Path& gameRoot, StringView set, StringView file) {
        if (file.empty()) {
            return {};
        }
        const String text = WithForwardSlashes(file);
        if (PathIsAbsolute(text) || text.front() == '/') {
            return Path(text);
        }
        for (size_t begin = 0; begin <= text.size();) {
            size_t end = text.find('/', begin);
            if (end == String::npos) {
                end = text.size();
            }
            if (StringView(text.data() + begin, end - begin) == "..") {
                return {};
            }
            begin = end + 1;
        }
        return SetFolder(gameRoot, set) / text;
    }

}  // namespace BECore
