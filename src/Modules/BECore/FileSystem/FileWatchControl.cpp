#include "FileWatchControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/FileSystem/FileWatchAnswers.h>
#include <BECore/FileSystem/FileWatcher.h>

namespace BECore {

    namespace {

        constexpr StringView kNotWatching = "not_watching";

        struct KindNames {
            static constexpr auto changed = "changed"_intern;
            static constexpr auto added = "added"_intern;
            static constexpr auto removed = "removed"_intern;
        };

        PoolString KindKey(FileChangeKind kind) {
            // No default: a new kind is a -Wswitch warning here, not a kind that shows as "changed"
            switch (kind) {
                case FileChangeKind::Changed:
                    return KindNames::changed;
                case FileChangeKind::Added:
                    return KindNames::added;
                case FileChangeKind::Removed:
                    return KindNames::removed;
            }
            return KindNames::changed;
        }

        /// The `paths` of a request into @p paths; false when it is not a list of strings. @p present tells whether it was given
        bool ReadPaths(const Json& params, Vector<String>& paths, bool& present) {
            const Json* member = FindJsonMember(params, "paths");
            present = member != nullptr;
            if (member == nullptr) {
                return true;
            }
            if (!member->is_array()) {
                return false;
            }
            for (const Json& item : *member) {
                if (!item.is_string()) {
                    return false;
                }
                paths.push_back(FromStdString(item.get_ref<const std::string&>()));  //-NoStd: nlohmann's own string type, read once at the edge
            }
            return true;
        }

        void HandleRescan(ControlCall& call, FileWatcher& watcher) {
            if (!watcher.IsRunning()) {
                call.Fail(kNotWatching, "the file watcher is not running (the fileWatch setting)");
                return;
            }
            FileWatchRescanAnswer answer;
            for (const PendingFileChange& change : watcher.Rescan()) {
                FileWatchRescanChange item;
                item.consumers = change.consumers;
                item.kind = KindKey(change.kind);
                item.path = change.virtualPath;
                answer.changes.push_back(std::move(item));
            }
            call.ReplyObject(answer);
        }

        void HandleApply(ControlCall& call, FileWatcher& watcher) {
            Vector<String> paths;
            bool hasPaths = false;
            if (!ReadPaths(call.Params(), paths, hasPaths)) {
                call.Fail(ControlErrors::kInvalidParams, "'paths' has to be a list of virtual paths");
                return;
            }
            if (!watcher.IsRunning()) {
                call.Fail(kNotWatching, "the file watcher is not running (the fileWatch setting)");
                return;
            }
            Vector<StringView> views;
            for (const String& path : paths) {
                views.push_back(StringView(path));
            }
            const Vector<AppliedFileChange> applied = hasPaths ? watcher.Apply(Span<const StringView>(views.data(), views.size())) : watcher.Apply();

            FileWatchApplyAnswer answer;
            for (const AppliedFileChange& change : applied) {
                FileWatchAppliedChange item;
                item.consumers = change.consumers;
                item.ok = change.ok;
                item.path = change.virtualPath;
                if (!change.ok) {
                    item.error = change.error;
                }
                answer.applied.push_back(std::move(item));
            }
            // The consumers announce what they took with events, which run in the frame: the answer waits for one, with the manual clock too
            call.DeferFrames(1, 0.0, [answer = std::move(answer)](ControlCall& done) { done.ReplyObject(answer); });
        }

    }  // namespace

    void RegisterFileWatchControl(ControlService& service, FileWatcher& watcher) {
        service.RegisterHandler("files_rescan", [&watcher](ControlCall& call) { HandleRescan(call, watcher); });
        service.RegisterHandler("files_apply", [&watcher](ControlCall& call) { HandleApply(call, watcher); });

        service.DescribeMethod("files_rescan", {"Lists the files that differ on disk from the state at start or their last delivery, without delivering anything", {}});
        service.DescribeMethod("files_apply", {"Delivers the changes of files_rescan to their consumers (a config is read again), answered after a frame",
                                               {{"paths", "array", false, "Virtual paths to apply; all pending changes when omitted"}}});
    }

}  // namespace BECore
