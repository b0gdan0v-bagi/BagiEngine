#pragma once

#include <RmlUi/Core/FileInterface.h>

namespace BECore {

    /**
     * @brief Rml::FileInterface over FileSystem: a path is a virtual path (`assets/ui/menu.rml`), layered like every other file of the engine.
     *
     * A file is read whole into memory when it is opened. A path that FileSystem cannot resolve is tried as a real path, so a test can point at a temporary directory.
     */
    class RmlUiFileInterface final : public Rml::FileInterface {
    public:
        Rml::FileHandle Open(const Rml::String& path) override;
        void Close(Rml::FileHandle file) override;
        size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
        bool Seek(Rml::FileHandle file, long offset, int origin) override;
        size_t Tell(Rml::FileHandle file) override;
        size_t Length(Rml::FileHandle file) override;

    private:
        struct OpenFile {
            eastl::vector<char> bytes;
            size_t position = 0;
        };

        eastl::hash_map<Rml::FileHandle, OpenFile> _files;
        Rml::FileHandle _nextHandle = 1;
    };

}  // namespace BECore
