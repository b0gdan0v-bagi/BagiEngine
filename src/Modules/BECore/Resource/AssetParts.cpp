#include "AssetParts.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/ReaderLimits.h>

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

        constexpr AssetPurpose kParts[] = {AssetPurpose::Sim, AssetPurpose::Visual, AssetPurpose::Editor};
        constexpr pugi::xml_encoding kEncoding = pugi::encoding_utf8;

        Optional<AssetPurpose> FindPart(StringView name) {
            for (const AssetPurpose part : kParts) {
                if (PartName(part) == name) {
                    return part;
                }
            }
            return {};
        }

        Optional<String>& Slot(AssetPartTexts& texts, AssetPurpose part) {
            return part == AssetPurpose::Sim ? texts.sim : part == AssetPurpose::Visual ? texts.visual : texts.editor;
        }

        String ToText(const pugi::xml_node& part) {
            pugi::xml_document document;
            document.append_copy(part);
            std::ostringstream stream;  //-NoStd: pugixml saves into an std::ostream
            document.save(stream, "  ", pugi::format_default | pugi::format_no_declaration);
            const std::string text = stream.str();  //-NoStd: the text of an std::ostringstream
            return String(text.c_str(), text.size());
        }

        std::expected<AssetPartTexts, String> SplitDocument(const pugi::xml_document& document) {
            AssetPartTexts texts;
            const pugi::xml_node root = document.document_element();
            if (!root) {
                return std::unexpected(String("The asset document has no root element"));
            }
            for (const pugi::xml_node child : root.children()) {
                if (child.type() != pugi::node_element) {
                    continue;
                }
                const Optional<AssetPurpose> part = FindPart(child.name());
                if (!part) {
                    return std::unexpected(Format("'{}' in the asset is no part (expected sim, visual or editor)", child.name()));
                }
                Optional<String>& slot = Slot(texts, *part);
                if (slot) {
                    return std::unexpected(Format("The part '{}' appears twice in the asset", child.name()));
                }
                slot = ToText(child);
            }
            if (!texts.sim && !texts.visual && !texts.editor) {
                return std::unexpected(String("The asset has no part (expected sim, visual or editor)"));
            }
            return texts;
        }

        /// The real folder for the split files: an existing virtual folder, else the folder inside its mount point
        std::filesystem::path ResolveOutputFolder(StringView virtualFolder) {
            const FileSystem& fileSystem = CoreManager::GetFileSystem();
            std::filesystem::path folder = fileSystem.ResolvePath(virtualFolder);
            if (!folder.empty()) {
                return folder;
            }
            const size_t slash = virtualFolder.find('/');
            const StringView mountPoint = virtualFolder.substr(0, slash);
            const std::filesystem::path mounted = fileSystem.GetMountedPath(PoolString::Intern(mountPoint));
            if (mounted.empty()) {
                return {};
            }
            folder = mounted;
            if (slash != StringView::npos) {
                folder /= std::filesystem::path(String(virtualFolder.substr(slash + 1)).c_str());
            }
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            return error ? std::filesystem::path{} : folder;
        }

        bool WriteText(const std::filesystem::path& file, const String& text) {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);  //-NoStd: the split files are plain text beside the source
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return out.good();
        }

    }  // namespace

    const Optional<String>& AssetPartTexts::Get(AssetPurpose part) const {
        return part == AssetPurpose::Sim ? sim : part == AssetPurpose::Visual ? visual : editor;
    }

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

    std::expected<AssetPartTexts, String> SplitAssetXml(StringView xml) {
        if (xml.size() > ReaderLimits::kMaxXmlDocumentBytes) {
            return std::unexpected(String("The asset document is too large"));
        }
        pugi::xml_document document;
        const pugi::xml_parse_result result = document.load_buffer(xml.data(), xml.size(), pugi::parse_default, kEncoding);
        if (!result) {
            return std::unexpected(Format("The asset document does not parse: {}", result.description()));
        }
        return SplitDocument(document);
    }

    bool SplitAssetFile(StringView virtualAssetPath, StringView outputVirtualFolder) {
        if (SplitPartPath(virtualAssetPath, AssetPurpose::Sim).empty()) {
            LOG_ERROR(Format("SplitAssetFile: '{}' is no .asset.xml file", virtualAssetPath).c_str());
            return false;
        }
        const std::filesystem::path source = CoreManager::GetFileSystem().ResolvePath(virtualAssetPath);
        std::error_code error;
        const auto size = source.empty() ? 0 : std::filesystem::file_size(source, error);
        if (source.empty() || error || size > ReaderLimits::kMaxXmlDocumentBytes) {
            LOG_ERROR(Format("SplitAssetFile: cannot read '{}'", virtualAssetPath).c_str());
            return false;
        }
        pugi::xml_document document;
        const pugi::xml_parse_result parsed = document.load_file(source.c_str(), pugi::parse_default, kEncoding);
        if (!parsed) {
            LOG_ERROR(Format("SplitAssetFile: '{}' does not parse: {}", virtualAssetPath, parsed.description()).c_str());
            return false;
        }
        const auto texts = SplitDocument(document);
        if (!texts) {
            LOG_ERROR(Format("SplitAssetFile: '{}': {}", virtualAssetPath, texts.error()).c_str());
            return false;
        }

        const std::filesystem::path folder = outputVirtualFolder.empty() ? source.parent_path() : ResolveOutputFolder(outputVirtualFolder);
        if (folder.empty()) {
            LOG_ERROR(Format("SplitAssetFile: no output folder '{}'", outputVirtualFolder).c_str());
            return false;
        }

        bool written = true;
        for (const AssetPurpose part : kParts) {
            const String splitPath = SplitPartPath(virtualAssetPath, part);
            const std::filesystem::path file = folder / std::filesystem::path(splitPath.c_str()).filename();
            const Optional<String>& text = texts->Get(part);
            if (!text) {
                std::filesystem::remove(file, error);
            } else if (!WriteText(file, *text)) {
                LOG_ERROR(Format("SplitAssetFile: cannot write '{}'", file.string()).c_str());
                written = false;
            }
        }
        return written;
    }

}  // namespace BECore
