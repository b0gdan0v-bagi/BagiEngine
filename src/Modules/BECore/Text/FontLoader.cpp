#include "FontLoader.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Text/Font.h>
#include <Generated/FontLoader.gen.hpp>
#include <TaskSystem/Awaitables.h>

namespace BECore {

    bool FontLoader::CanLoad(StringView extension) const {
        return extension == ".ttf" || extension == ".otf";
    }

    Task<IntrusivePtr<IResource>> FontLoader::LoadAsync(PoolString path) {
        co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Load font"));
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> FontLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<Font> FontLoader::LoadInternal(PoolString path) {
        BE_PROFILE_SCOPE("FontLoader::Load");
        const auto realPath = CoreManager::GetFileSystem().ResolvePath(path.ToStringView());
        if (realPath.Empty()) {
            LOG_ERROR("FontLoader: cannot open '{}'", path.ToStringView());
            return Font::CreateFromMemory(path, {});
        }
        auto bytes = ReadFileBytes(realPath);
        if (!bytes) {
            LOG_ERROR("FontLoader: cannot read '{}': {}", path.ToStringView(), bytes.error());
            return Font::CreateFromMemory(path, {});
        }

        auto font = Font::CreateFromMemory(path, std::move(*bytes));
        if (font->GetState() != ResourceState::Loaded) {
            LOG_ERROR("FontLoader: '{}' is not a font file that stb_truetype can read", path.ToStringView());
            return font;
        }
        LOG_INFO("FontLoader: loaded font '{}'", path.ToStringView());
        return font;
    }

}  // namespace BECore
