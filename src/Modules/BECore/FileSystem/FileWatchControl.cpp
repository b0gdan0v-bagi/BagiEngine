#include "FileWatchControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/FileSystem/FileWatcher.h>

namespace BECore {

    namespace {

        constexpr StringView kNotWatching = "not_watching";

        StringView KindKey(FileChangeKind kind) {
            // No default: a new kind is a -Wswitch warning here, not a kind that shows as "changed"
            switch (kind) {
                case FileChangeKind::Changed:
                    return "changed";
                case FileChangeKind::Added:
                    return "added";
                case FileChangeKind::Removed:
                    return "removed";
            }
            return "changed";
        }

        Json ConsumersJson(const Vector<PoolString>& consumers) {
            Json result = Json::array();
            for (const PoolString consumer : consumers) {
                result.push_back(MakeJsonString(consumer.ToStringView()));
            }
            return result;
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
            Json changes = Json::array();
            for (const PendingFileChange& change : watcher.Rescan()) {
                Json item = Json::object();
                item["path"] = MakeJsonString(change.virtualPath.ToStringView());
                item["kind"] = MakeJsonString(KindKey(change.kind));
                item["consumers"] = ConsumersJson(change.consumers);
                changes.push_back(std::move(item));
            }
            Json result = Json::object();
            result["changes"] = std::move(changes);
            call.Reply(std::move(result));
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

            Json items = Json::array();
            for (const AppliedFileChange& change : applied) {
                Json item = Json::object();
                item["path"] = MakeJsonString(change.virtualPath.ToStringView());
                item["consumers"] = ConsumersJson(change.consumers);
                item["ok"] = change.ok;
                if (!change.ok) {
                    item["error"] = MakeJsonString(change.error);
                }
                items.push_back(std::move(item));
            }
            Json result = Json::object();
            result["applied"] = std::move(items);
            // The consumers announce what they took with events, which run in the frame: the answer waits for one, with the manual clock too
            call.DeferFrames(1, 0.0, [result = std::move(result)](ControlCall& done) mutable { done.Reply(std::move(result)); });
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
