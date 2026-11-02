#pragma once

#include <RmlUi/Core/FileInterface.h>

namespace BECore {

    /// A file as it was when a document read it: where it is and when it was last written. A document that came from files is loaded again when one of them is stale
    struct RmlUiFileStamp {
        std::filesystem::path path;
        std::filesystem::file_time_type lastWrite{};
        uintmax_t size = 0;

        /// The stamp of @p path now (an empty one for a file that cannot be read)
        [[nodiscard]] static RmlUiFileStamp Of(const std::filesystem::path& path);

        /// Whether the file on disk differs from this stamp. A file that cannot be read now (an editor saves by deleting and renaming) is not stale: it is looked at again later
        [[nodiscard]] bool IsStale() const;
    };

    /**
     * @brief Rml::FileInterface over FileSystem: a path is a virtual path (`assets/ui/menu.rml`), layered like every other file of the engine.
     *
     * A file is read whole into memory when it is opened. A path that FileSystem cannot resolve is tried as a real path, so a test can point at a temporary directory.
     *
     * Between BeginRecording() and EndRecording() it notes the stamp of every file it opens (taken before the file is read, so a write that comes after is seen as a change):
     * these are the files a document is made of.
     */
    class RmlUiFileInterface final : public Rml::FileInterface {
    public:
        Rml::FileHandle Open(const Rml::String& path) override;
        void Close(Rml::FileHandle file) override;
        size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
        bool Seek(Rml::FileHandle file, long offset, int origin) override;
        size_t Tell(Rml::FileHandle file) override;
        size_t Length(Rml::FileHandle file) override;

        /// Starts a new list of the files opened from now on
        void BeginRecording();

        /// @return the files opened since BeginRecording(), each once, in the order they were opened
        [[nodiscard]] eastl::vector<RmlUiFileStamp> EndRecording();

    private:
        struct OpenFile {
            eastl::vector<char> bytes;
            size_t position = 0;
        };

        eastl::hash_map<Rml::FileHandle, OpenFile> _files;
        Rml::FileHandle _nextHandle = 1;
        eastl::vector<RmlUiFileStamp> _recorded;
        bool _isRecording = false;
    };

}  // namespace BECore
