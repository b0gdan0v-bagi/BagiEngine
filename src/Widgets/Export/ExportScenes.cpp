#include "ExportScenes.h"

#include <BECore/Config/XmlDocument.h>
#include <BECore/File/File.h>
#include <BECore/Scene/SceneLoader.h>

namespace BECore {

    namespace {

        constexpr StringView kXmlExtension = ".xml";
        constexpr StringView kCookedExtension = ".bin";

        Path SceneFile(const Path& layer, StringView extension) {
            return layer / Format("{}{}", kSceneConfigName, extension);
        }

        bool IsFile(const Path& path) {
            const auto info = GetFileInfo(path);
            return info && info->kind == FileKind::File;
        }

        std::expected<Vector<String>, String> ReadSceneNames(const Path& file) {
            XmlDocument document;
            if (!document.LoadFromFile(file)) {
                return std::unexpected(Format("{}{} cannot be read", kSceneConfigName, kXmlExtension));
            }
            const XmlNode scenes = document.GetRoot().GetChild("scenes");
            if (!scenes) {
                return std::unexpected(Format("{}{} has no <scenes>", kSceneConfigName, kXmlExtension));
            }
            Vector<String> names;
            for (const XmlNode scene : scenes.Children()) {
                const StringView name = scene.GetAttribute("name").value_or(StringView{});
                names.emplace_back(name.data(), name.size());
            }
            return names;
        }

    }  // namespace

    std::expected<Vector<String>, String> ReadExportScenes(Span<const Path> configLayers) {
        for (size_t above = configLayers.size(); above > 0; --above) {
            const Path& layer = configLayers[above - 1];
            if (const Path xml = SceneFile(layer, kXmlExtension); IsFile(xml)) {
                return ReadSceneNames(xml);
            }
            if (IsFile(SceneFile(layer, kCookedExtension))) {
                return std::unexpected(Format("the game reads the cooked {}{}", kSceneConfigName, kCookedExtension));
            }
        }
        return std::unexpected(Format("no content root has config/{}{}", kSceneConfigName, kXmlExtension));
    }

}  // namespace BECore
