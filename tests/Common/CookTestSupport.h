#pragma once

#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Format/Format.h>
#include <BECore/Types/Types.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <filesystem>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /**
     * A temporary two-layer `config` tree for a test of the cook: `source<k>/config` is the layer k of a FileSystem of its own (0 the
     * lowest), `out` is where a cook writes. One folder per test and per process (TempPath.h). Nothing here touches the process
     * registry of cookers: a test gives `CookContent` a registry of its own.
     */
    class CookTestTree {
    public:
        static constexpr size_t kLayers = 2;

        CookTestTree() : _root(TestTempDirectory("cook")) {
            EngineEnvironment::EnsureCoreServices();
            MakeEmptyDirectory(_root);
            for (size_t layer = 0; layer < kLayers; ++layer) {
                MakeDirectory(SourceConfig(layer));
                EXPECT_TRUE(_sources.Mount(MountPoints::config, SourceConfig(layer)));
            }
        }

        ~CookTestTree() {
            RemoveDirectory(_root);
        }

        CookTestTree(const CookTestTree&) = delete;
        CookTestTree& operator=(const CookTestTree&) = delete;

        std::filesystem::path SourceConfig(size_t layer) const {
            return _root / ToStd(Format("source{}", layer)) / "config";
        }

        /// Writes @p text as the file @p relative of the source layer @p layer
        void Write(size_t layer, StringView relative, StringView text) const {
            WriteFile(SourceConfig(layer) / ToStd(relative), text);
        }

        const FileSystem& Sources() const {
            return _sources;
        }

        std::filesystem::path Out() const {
            return _root / "out";
        }

        std::filesystem::path OutConfig(size_t layer) const {
            return Out() / ToStd(Format("layer{}", layer)) / "config";
        }

        std::filesystem::path Root() const {
            return _root;
        }

    private:
        std::filesystem::path _root;
        FileSystem _sources;
    };

    /// A FileSystem and a ConfigManager over the cooked layers of a cook (`<out>/layer<k>/config`, k < @p layers), as the game would load them
    class CookedContent {
    public:
        CookedContent(const std::filesystem::path& out, size_t layers) {
            for (size_t layer = 0; layer < layers; ++layer) {
                EXPECT_TRUE(_fileSystem.Mount(MountPoints::config, out / ToStd(Format("layer{}", layer)) / "config")) << "layer " << layer;
            }
            _configs.Initialize();
        }

        FileSystem& Files() {
            return _fileSystem;
        }

        ConfigManager& Configs() {
            return _configs;
        }

    private:
        FileSystem _fileSystem;
        ConfigManager _configs{_fileSystem};
    };

    /// A ConfigManager over the source layers of @p tree: what the XML content reads as
    class SourceContent {
    public:
        explicit SourceContent(const CookTestTree& tree) : _configs(tree.Sources()) {
            _configs.Initialize();
        }

        ConfigManager& Configs() {
            return _configs;
        }

    private:
        ConfigManager _configs;
    };

}  // namespace BECore::Tests
