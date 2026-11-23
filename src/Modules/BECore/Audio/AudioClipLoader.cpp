#include "AudioClipLoader.h"

#include <BECore/Audio/AudioClip.h>
#include <BECore/File/File.h>
#include <BECore/File/FilePath.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/ScratchArena.h>
#include <Generated/AudioClipLoader.gen.hpp>
#include <TaskSystem/Awaitables.h>

namespace BECore {

    bool AudioClipLoader::CanLoad(StringView extension) const {
        return extension == ".wav" || extension == ".ogg" || extension == ".flac";
    }

    Task<IntrusivePtr<IResource>> AudioClipLoader::LoadAsync(PoolString path) {
        co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Load audio clip"));
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> AudioClipLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    namespace {

        /// The largest clip file read into the thread's scratch arena; a bigger one is read to the heap, because a pool thread's scratch
        /// arena keeps the chunk of its biggest clip after the scope ends
        constexpr size_t kMaxScratchClipBytes = size_t{1} * 1024 * 1024;

        /// The real path of a virtual @p path, or the sentence for the log when it does not resolve
        std::expected<String, String> ResolveAudioPath(PoolString path) {
            const auto realPath = CoreManager::GetFileSystem().ResolvePath(path.ToStringView());
            if (realPath.Empty()) {
                return std::unexpected(Format("cannot open '{}'", path.ToStringView()));
            }
            return PathToUtf8(realPath);
        }

    }  // namespace

    std::expected<Vector<uint8_t>, String> ReadAudioFile(PoolString path) {
        const auto realPath = ResolveAudioPath(path);
        if (!realPath) {
            return std::unexpected(realPath.error());
        }
        auto bytes = ReadFileBytes(*realPath);
        if (!bytes) {
            return std::unexpected(Format("cannot read '{}': {}", path.ToStringView(), bytes.error()));
        }
        return std::move(*bytes);
    }

    IntrusivePtr<AudioClip> AudioClipLoader::LoadInternal(PoolString path) {
        BE_PROFILE_SCOPE("AudioClipLoader::Load");
        // The file bytes die with the decode: only the decoded samples are kept. No co_await here: a scratch scope ends on its own thread
        ScratchScope scratch;
        const auto realPath = ResolveAudioPath(path);
        if (!realPath) {
            LOG_ERROR("AudioClipLoader: {}", realPath.error());
            return AudioClip::CreateFailed(path);
        }
        const auto bytes = ReadFileBytesScratch(*realPath, scratch.GetArena(), kMaxScratchClipBytes);
        if (!bytes) {
            LOG_ERROR("AudioClipLoader: cannot read '{}': {}", path.ToStringView(), bytes.error());
            return AudioClip::CreateFailed(path);
        }

        auto decoded = DecodeAudio(bytes->Bytes());
        if (!decoded) {
            LOG_ERROR("AudioClipLoader: '{}': {}", path.ToStringView(), decoded.error());
            return AudioClip::CreateFailed(path);
        }
        return AudioClip::Create(path, std::move(*decoded));
    }

}  // namespace BECore
