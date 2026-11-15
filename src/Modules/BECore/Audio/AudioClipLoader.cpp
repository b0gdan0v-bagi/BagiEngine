#include "AudioClipLoader.h"

#include <BECore/Audio/AudioClip.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <Generated/AudioClipLoader.gen.hpp>
#include <TaskSystem/Awaitables.h>
#include <fstream>  //-NoStd: file layer (engine-file-layer)

namespace BECore {

    bool AudioClipLoader::CanLoad(StringView extension) const {
        return extension == ".wav" || extension == ".ogg";
    }

    Task<IntrusivePtr<IResource>> AudioClipLoader::LoadAsync(PoolString path) {
        co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Load audio clip"));
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> AudioClipLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<AudioClip> AudioClipLoader::LoadInternal(PoolString path) {
        BE_PROFILE_SCOPE("AudioClipLoader::Load");
        const auto realPath = CoreManager::GetFileSystem().ResolvePath(path.ToStringView());
        std::ifstream file(realPath, std::ios::binary | std::ios::ate);  //-NoStd: file layer (engine-file-layer)
        if (realPath.empty() || !file) {
            LOG_ERROR("AudioClipLoader: cannot open '{}'"_format(path.ToStringView()));
            return AudioClip::CreateFailed(path);
        }
        const auto size = static_cast<size_t>(file.tellg());
        Vector<uint8_t> bytes(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (!file) {
            LOG_ERROR("AudioClipLoader: cannot read '{}'"_format(path.ToStringView()));
            return AudioClip::CreateFailed(path);
        }

        auto decoded = DecodeAudio(bytes);
        if (!decoded) {
            LOG_ERROR("AudioClipLoader: '{}': {}"_format(path.ToStringView(), decoded.error()));
            return AudioClip::CreateFailed(path);
        }
        return AudioClip::Create(path, std::move(*decoded));
    }

}  // namespace BECore
