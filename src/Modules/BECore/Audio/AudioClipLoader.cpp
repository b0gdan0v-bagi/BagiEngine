#include "AudioClipLoader.h"

#include <BECore/Audio/AudioClip.h>
#include <BECore/File/File.h>
#include <BECore/File/FilePath.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
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

    std::expected<Vector<uint8_t>, String> ReadAudioFile(PoolString path) {
        const auto realPath = CoreManager::GetFileSystem().ResolvePath(path.ToStringView());
        if (realPath.empty()) {
            return std::unexpected(Format("cannot open '{}'", path.ToStringView()));
        }
        auto bytes = ReadFileBytes(PathToUtf8(realPath));
        if (!bytes) {
            return std::unexpected(Format("cannot read '{}': {}", path.ToStringView(), bytes.error()));
        }
        return std::move(*bytes);
    }

    IntrusivePtr<AudioClip> AudioClipLoader::LoadInternal(PoolString path) {
        BE_PROFILE_SCOPE("AudioClipLoader::Load");
        auto bytes = ReadAudioFile(path);
        if (!bytes) {
            LOG_ERROR("AudioClipLoader: {}", bytes.error());
            return AudioClip::CreateFailed(path);
        }

        auto decoded = DecodeAudio(*bytes);
        if (!decoded) {
            LOG_ERROR("AudioClipLoader: '{}': {}", path.ToStringView(), decoded.error());
            return AudioClip::CreateFailed(path);
        }
        return AudioClip::Create(path, std::move(*decoded));
    }

}  // namespace BECore
