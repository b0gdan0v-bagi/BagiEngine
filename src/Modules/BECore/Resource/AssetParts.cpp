#include "AssetParts.h"

#include <BECore/Config/StringXmlWriter.h>
#include <BECore/File/File.h>
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
        bool IsFresh(const Path& splitFile, const Path& wholeFile) {
            const Optional<FileInfo> split = GetFileInfo(splitFile);
            const Optional<FileInfo> whole = GetFileInfo(wholeFile);
            return split && whole && split->modifyTimeNs >= whole->modifyTimeNs;
        }

        /// The virtual path of the split file that serves @p part of @p assetPath, empty when there is none
        String FindSplitFile(StringView assetPath, AssetPurpose part) {
            String splitPath = SplitPartPath(assetPath, part);
            if (splitPath.empty()) {
                return {};
            }
            const FileSystem& fileSystem = CoreManager::GetFileSystem();
            const Path splitReal = fileSystem.ResolvePath(splitPath);
            if (splitReal.Empty()) {
                return {};
            }
            const Path wholeReal = fileSystem.ResolvePath(assetPath);
            if (!wholeReal.Empty() && !IsFresh(splitReal, wholeReal)) {
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
            StringXmlWriter writer;
            document.save(writer, "  ", pugi::format_default | pugi::format_no_declaration);
            return eastl::move(writer.text);
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
        Path ResolveOutputFolder(StringView virtualFolder) {
            const FileSystem& fileSystem = CoreManager::GetFileSystem();
            Path folder = fileSystem.ResolvePath(virtualFolder);
            if (!folder.Empty()) {
                return folder;
            }
            const size_t slash = virtualFolder.find('/');
            const StringView mountPoint = virtualFolder.substr(0, slash);
            const Path mounted = fileSystem.GetMountedPath(PoolString::Intern(mountPoint));
            if (mounted.Empty()) {
                return {};
            }
            folder = mounted;
            if (slash != StringView::npos) {
                folder /= virtualFolder.substr(slash + 1);
            }
            return CreateDirectories(folder) ? folder : Path{};
        }

        bool WriteText(const Path& file, const String& text) {
            // In place, not atomic: the split files sit beside the source and a half-written one is rewritten by the next split
            auto out = File::Open(file, FileMode::Write);
            return out && out->Write(StringView(text)) && out->Close().has_value();
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
            LOG_ERROR("SplitAssetFile: '{}' is no .asset.xml file", virtualAssetPath);
            return false;
        }
        const Path source = CoreManager::GetFileSystem().ResolvePath(virtualAssetPath);
        const Optional<FileInfo> info = source.Empty() ? Optional<FileInfo>{} : GetFileInfo(source);
        if (!info || info->kind != FileKind::File || info->size > ReaderLimits::kMaxXmlDocumentBytes) {
            LOG_ERROR("SplitAssetFile: cannot read '{}'", virtualAssetPath);
            return false;
        }
        // load_file(const char*) is the ANSI code page on Windows, which a UTF-8 path is not
        const std::expected<Vector<uint8_t>, String> bytes = ReadFileBytes(source, ReaderLimits::kMaxXmlDocumentBytes);
        if (!bytes) {
            LOG_ERROR("SplitAssetFile: cannot read '{}'", virtualAssetPath);
            return false;
        }
        pugi::xml_document document;
        const pugi::xml_parse_result parsed = document.load_buffer(bytes->data(), bytes->size(), pugi::parse_default, kEncoding);
        if (!parsed) {
            LOG_ERROR("SplitAssetFile: '{}' does not parse: {}", virtualAssetPath, parsed.description());
            return false;
        }
        const auto texts = SplitDocument(document);
        if (!texts) {
            LOG_ERROR("SplitAssetFile: '{}': {}", virtualAssetPath, texts.error());
            return false;
        }

        const Path folder = outputVirtualFolder.empty() ? source.Parent() : ResolveOutputFolder(outputVirtualFolder);
        if (folder.Empty()) {
            LOG_ERROR("SplitAssetFile: no output folder '{}'", outputVirtualFolder);
            return false;
        }

        bool written = true;
        for (const AssetPurpose part : kParts) {
            const String splitPath = SplitPartPath(virtualAssetPath, part);
            const Path file = folder / PathFilename(splitPath);
            const Optional<String>& text = texts->Get(part);
            if (!text) {
                static_cast<void>(RemoveFile(file));  // a part that never was there is not an error
            } else if (!WriteText(file, *text)) {
                LOG_ERROR("SplitAssetFile: cannot write '{}'", file.View());
                written = false;
            }
        }
        return written;
    }

}  // namespace BECore
