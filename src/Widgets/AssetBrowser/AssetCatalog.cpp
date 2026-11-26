#include "AssetCatalog.h"

#include <BECore/Config/XmlDocument.h>
#include <BECore/File/Path.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr bool IsSameValue(AssetTypeFilter filter, AssetKind kind) {
            return static_cast<uint8_t>(filter) == static_cast<uint8_t>(kind);
        }

        static_assert(IsSameValue(AssetTypeFilter::Texture, AssetKind::Texture) && IsSameValue(AssetTypeFilter::Other, AssetKind::Other), "a filter value names the kind it lets through");

        struct AssetExtension {
            StringView extension;
            AssetKind kind = AssetKind::None;
        };

        /// Every extension the browser lists, with the kind a file of it has unless a rule of `AssetKindOfName` says otherwise
        constexpr AssetExtension kAssetExtensions[] = {
            {".png", AssetKind::Texture}, {".jpg", AssetKind::Texture}, {".jpeg", AssetKind::Texture}, {".bmp", AssetKind::Texture}, {".ttf", AssetKind::Font},
            {".otf", AssetKind::Font},    {".wav", AssetKind::Sound},   {".ogg", AssetKind::Sound},    {".flac", AssetKind::Sound},  {".html", AssetKind::UiDocument},
            {".css", AssetKind::Other},   {".rcss", AssetKind::Other},  {".rml", AssetKind::Other},    {".json", AssetKind::Other},  {".xml", AssetKind::Other},
        };

        constexpr StringView kXmlExtension = ".xml";
        constexpr StringView kUiXmlSuffix = ".ui.xml";
        constexpr StringView kSidecarSuffix = ".artgen.json";
        /// The engine reads the scenes from the file of this name
        constexpr StringView kSceneFileName = "SceneConfig.xml";

        char ToLowerAscii(char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool EqualsIgnoreCase(StringView left, StringView right) {
            return left.size() == right.size() && eastl::equal(left.begin(), left.end(), right.begin(), [](char a, char b) { return ToLowerAscii(a) == ToLowerAscii(b); });
        }

        bool EndsWithIgnoreCase(StringView text, StringView suffix) {
            return text.size() >= suffix.size() && EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
        }

        bool ContainsIgnoreCase(StringView text, StringView needle) {
            for (size_t start = 0; start + needle.size() <= text.size(); ++start) {
                if (EqualsIgnoreCase(text.substr(start, needle.size()), needle)) {
                    return true;
                }
            }
            return false;
        }

        /// What a `.xml` under `config` is: an unreadable file is a config like any other
        AssetKind KindOfConfigXml(const FileSystem& fileSystem, StringView path) {
            if (PathFilename(path) == kSceneFileName) {
                return AssetKind::Scene;
            }
            XmlDocument document;
            if (document.LoadFromFile(fileSystem.ResolvePath(path)) && document.GetRoot().Name() == kNodeElementName) {
                return AssetKind::Node;
            }
            return AssetKind::Config;
        }

    }  // namespace

    Span<const PoolString> GetAssetMounts() {
        static const Array<PoolString, 2> mounts = {MountPoints::assets, MountPoints::config};
        return {mounts.data(), mounts.size()};
    }

    bool IsAssetSidecar(StringView virtualPath) {
        return EndsWithIgnoreCase(PathFilename(virtualPath), kSidecarSuffix);
    }

    AssetKind AssetKindOfName(StringView virtualPath) {
        const StringView name = PathFilename(virtualPath);
        if (IsAssetSidecar(name)) {
            return AssetKind::None;
        }
        if (EndsWithIgnoreCase(name, kUiXmlSuffix)) {
            return AssetKind::UiDocument;
        }
        const StringView extension = PathExtension(name);
        for (const AssetExtension& listed : kAssetExtensions) {
            if (!EqualsIgnoreCase(extension, listed.extension)) {
                continue;
            }
            if (listed.extension == kXmlExtension && PathIsUnder(virtualPath, MountPoints::config.ToPoolString().ToStringView())) {
                return AssetKind::Config;
            }
            return listed.kind;
        }
        return AssetKind::None;
    }

    void AssetCatalog::Rebuild(const FileSystem& fileSystem) {
        Array<StringView, eastl::size(kAssetExtensions)> extensions;
        for (size_t i = 0; i < extensions.size(); ++i) {
            extensions[i] = kAssetExtensions[i].extension;
        }

        _files.clear();
        _folders.clear();
        StringView lastParent;
        for (const PoolString mount : GetAssetMounts()) {
            for (const PoolString path : fileSystem.EnumerateFiles(mount, Span<const StringView>(extensions.data(), extensions.size()))) {
                const StringView text = path.ToStringView();
                AssetKind kind = AssetKindOfName(text);
                if (kind == AssetKind::None) {
                    continue;
                }
                if (kind == AssetKind::Config) {
                    kind = KindOfConfigXml(fileSystem, text);
                }
                _files.push_back({path, kind});

                // Files of one folder mostly follow each other; the repeats that are left go with the sort below
                const StringView parent = PathParent(text);
                if (parent == lastParent) {
                    continue;
                }
                lastParent = parent;
                for (StringView folder = parent; !folder.empty(); folder = PathParent(folder)) {
                    const auto depth = static_cast<uint32_t>(eastl::count(folder.begin(), folder.end(), '/'));
                    _folders.push_back({PoolString::Intern(folder), depth});
                }
            }
        }

        // PathCompare goes name by name, so a folder sorts right before what is inside it
        eastl::sort(_folders.begin(), _folders.end(), [](const AssetFolder& left, const AssetFolder& right) { return PathCompare(left.path.ToStringView(), right.path.ToStringView()) < 0; });
        _folders.erase(eastl::unique(_folders.begin(), _folders.end(), [](const AssetFolder& left, const AssetFolder& right) { return left.path == right.path; }), _folders.end());
    }

    const AssetFile* AssetCatalog::Find(StringView path) const {
        for (const AssetFile& file : _files) {
            if (file.path.ToStringView() == path) {
                return &file;
            }
        }
        return nullptr;
    }

    bool AssetCatalog::HasFolder(StringView path) const {
        return eastl::any_of(_folders.begin(), _folders.end(), [path](const AssetFolder& folder) { return folder.path.ToStringView() == path; });
    }

    void AssetCatalog::CollectShown(StringView folder, StringView search, AssetTypeFilter filter, Vector<uint32_t>& out) const {
        out.clear();
        const bool directOnly = search.empty() && filter == AssetTypeFilter::All;
        for (size_t i = 0; i < _files.size(); ++i) {
            const StringView path = _files[i].path.ToStringView();
            if (directOnly) {
                if (PathParent(path) != folder) {
                    continue;
                }
            } else {
                if (!folder.empty() && !PathIsUnder(path, folder)) {
                    continue;
                }
                if (filter != AssetTypeFilter::All && !IsSameValue(filter, _files[i].kind)) {
                    continue;
                }
                if (!ContainsIgnoreCase(PathFilename(path), search)) {
                    continue;
                }
            }
            out.push_back(static_cast<uint32_t>(i));
        }
    }

}  // namespace BECore
