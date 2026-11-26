#include "PackagedContent.h"

#include <BECore/File/File.h>
#include <SDL3/SDL_filesystem.h>

namespace BECore {

    Optional<PackagedContentRoots> FindPackagedContentRoots(const Path& folder) {
        // An empty folder would join into a relative path, and the working directory must not decide what the executable reads
        if (folder.Empty()) {
            return {};
        }
        const Optional<FileInfo> marker = GetFileInfo((folder / PackageLayout::kMarkerFile).View());
        if (!marker || marker->kind != FileKind::File) {
            return {};
        }
        const Path content = folder / PackageLayout::kContentFolder;
        return PackagedContentRoots{content / PackageLayout::kEngineFolder, content / PackageLayout::kGameFolder};
    }

    Path ExecutableDirectory() {
        const char* basePath = SDL_GetBasePath();  // owned by SDL
        return basePath != nullptr ? Path(basePath) : Path();
    }

}  // namespace BECore
