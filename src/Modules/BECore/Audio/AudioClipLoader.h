#pragma once

#include <BECore/Resource/IResourceLoader.h>

namespace BECore {

    class AudioClip;

    /// Loads .wav and .ogg files into AudioClip resources: reads the bytes and decodes them whole on the CPU, so LoadAsync() runs on a pool thread
    class AudioClipLoader : public IResourceLoader {
        BE_CLASS(AudioClipLoader)
    public:
        AudioClipLoader() = default;
        ~AudioClipLoader() override = default;

        bool CanLoad(StringView extension) const override;
        Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override;
        IntrusivePtr<IResource> LoadSync(PoolString path) override;

    private:
        static IntrusivePtr<AudioClip> LoadInternal(PoolString path);
    };

}  // namespace BECore
