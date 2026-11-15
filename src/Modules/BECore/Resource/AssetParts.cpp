#include "AssetParts.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/OpenDeserializer.h>

namespace BECore {

    namespace {

        constexpr StringView kAssetSuffix = ".asset.xml";
        constexpr StringView kSplitExtension = ".xml";

        bool EndsWith(StringView text, StringView suffix) {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        /// The split file serves only when it is not older than the whole file; a time that cannot be read counts as older.
        bool IsFresh(const std::filesystem::path& splitFile, const std::filesystem::path& wholeFile) {
            std::error_code error;
            const auto splitTime = std::filesystem::last_write_time(splitFile, error);
            if (error) {
                return false;
            }
            const auto wholeTime = std::filesystem::last_write_time(wholeFile, error);
            return !error && splitTime >= wholeTime;
        }

        /// The virtual path of the split file that serves @p part of @p assetPath, empty when there is none
        String FindSplitFile(StringView assetPath, AssetPurpose part) {
            String splitPath = SplitPartPath(assetPath, part);
            if (splitPath.empty()) {
                return {};
            }
            const FileSystem& fileSystem = CoreManager::GetFileSystem();
            const std::filesystem::path splitReal = fileSystem.ResolvePath(splitPath);
            if (splitReal.empty()) {
                return {};
            }
            const std::filesystem::path wholeReal = fileSystem.ResolvePath(assetPath);
            if (!wholeReal.empty() && !IsFresh(splitReal, wholeReal)) {
                return {};
            }
            return splitPath;
        }

    }  // namespace

    String SplitPartPath(StringView assetPath, AssetPurpose part) {
        const StringView name = PartName(part);
        if (name.empty() || !EndsWith(assetPath, kAssetSuffix)) {
            return {};
        }
        const StringView stem = assetPath.substr(0, assetPath.size() - kAssetSuffix.size());
        return Format("{}.{}{}", stem, name, kSplitExtension);
    }

    std::expected<UniquePtr<IDeserializer>, String> OpenAssetPart(StringView assetPath, AssetPurpose part) {
        const StringView name = PartName(part);
        if (name.empty()) {
            return std::unexpected(Format("'{}' is no part of an asset: '{}'", PurposeName(part), assetPath));
        }

        const String splitPath = FindSplitFile(assetPath, part);
        auto opened = OpenDeserializer(splitPath.empty() ? assetPath : StringView(splitPath));
        if (!opened) {
            return std::unexpected(eastl::move(opened.error()));
        }
        (*opened)->SetPurposeFilter(part);
        if (splitPath.empty() && !(*opened)->BeginObject(name)) {
            return UniquePtr<IDeserializer>{};
        }
        return eastl::move(*opened);
    }

}  // namespace BECore
