#include "FontLoader.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Text/Font.h>
#include <Generated/FontLoader.gen.hpp>
#include <TaskSystem/Awaitables.h>

namespace BECore {

    bool FontLoader::CanLoad(eastl::string_view extension) const {
        return extension == ".ttf" || extension == ".otf";
    }

    Task<IntrusivePtr<IResource>> FontLoader::LoadAsync(PoolString path) {
        co_await SwitchToBackground();
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> FontLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<Font> FontLoader::LoadInternal(PoolString path) {
        BE_PROFILE_SCOPE("FontLoader::Load");
        const auto realPath = CoreManager::GetFileSystem().ResolvePath(path.ToStringView());
        std::ifstream file(realPath, std::ios::binary | std::ios::ate);
        if (realPath.empty() || !file) {
            LOG_ERROR("FontLoader: cannot open '{}'"_format(path.ToStringView()));
            return Font::CreateFromMemory(path, {});
        }
        const auto size = static_cast<size_t>(file.tellg());
        eastl::vector<uint8_t> bytes(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (!file) {
            LOG_ERROR("FontLoader: cannot read '{}'"_format(path.ToStringView()));
            return Font::CreateFromMemory(path, {});
        }

        auto font = Font::CreateFromMemory(path, std::move(bytes));
        if (font->GetState() != ResourceState::Loaded) {
            LOG_ERROR("FontLoader: '{}' is not a font file that stb_truetype can read"_format(path.ToStringView()));
            return font;
        }
        LOG_INFO("FontLoader: loaded font '{}'"_format(path.ToStringView()));
        return font;
    }

}  // namespace BECore
