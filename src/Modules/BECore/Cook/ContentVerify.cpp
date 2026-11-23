#include "ContentVerify.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ContentVersion.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/File/File.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Scene/SceneNodeInstance.h>

namespace BECore {

    namespace {

        constexpr StringView kXmlExtension = ".xml";
        constexpr StringView kBinaryExtension = ".bin";
        constexpr StringView kLayerFolderPrefix = "layer";
        constexpr StringView kConfigFolder = "config";
        constexpr StringView kEndOfText = "<end of text>";
        constexpr StringView kEllipsis = "...";

        void Fail(CookReport& report, size_t layer, StringView path, String message) {
            CookRow& row = report.rows.emplace_back();
            row.layer = layer;
            row.path = String(path);
            row.outcome = CookOutcome::Failed;
            row.messages.push_back(eastl::move(message));
        }

        bool IsDirectory(const Path& path) {
            const Optional<FileInfo> info = GetFileInfo(path);
            return info.has_value() && info->kind == FileKind::Directory;
        }

        Vector<StringView> SplitLines(StringView text) {
            Vector<StringView> lines;
            size_t begin = 0;
            while (begin <= text.size()) {
                const size_t end = text.find('\n', begin);
                if (end == StringView::npos) {
                    if (begin < text.size()) {
                        lines.push_back(text.substr(begin));
                    }
                    break;
                }
                lines.push_back(text.substr(begin, end - begin));
                begin = end + 1;
            }
            return lines;
        }

        /// The line @p index of @p lines in quotes and cut to the limit, or the end-of-text marker past the last line
        String QuoteLine(const Vector<StringView>& lines, size_t index) {
            if (index >= lines.size()) {
                return String(kEndOfText);
            }
            const StringView line = lines[index];
            if (line.size() <= kMaxQuotedLineLength) {
                return Format("'{}'", line);
            }
            return Format("'{}{}'", line.substr(0, kMaxQuotedLineLength), kEllipsis);
        }

        Path CookedLayerFolder(const Path& cookedDir, size_t layer) {
            return cookedDir / Format("{}{}", kLayerFolderPrefix, layer) / kConfigFolder;
        }

        /// The reason a `.bin` is refused: the checks `ConfigManager` makes of a file it loads, with the message of the first that fails
        String RefusalReason(const Path& path) {
            Vector<uint8_t> bytes;
            if (const std::expected<void, StringView> read = ReadBinaryDocumentFile(path, bytes); !read.has_value()) {
                return String(read.error());
            }
            BinaryDeserializer reader;
            if (!reader.LoadFromView(bytes.data(), bytes.size())) {
                const auto& errors = reader.GetErrors();
                return errors.empty() ? String("the file is not a binary document") : errors.front().errorMessage;
            }
            if (const auto stamp = CheckContentStamp(reader, path.View()); !stamp) {
                return stamp.error();
            }
            return "ConfigManager did not load the file";
        }

        /// What `ConfigManager` makes of the cooked layer: a file system of its own over that one folder, so a file is read the way the game reads a layer
        struct CookedLayerView {
            explicit CookedLayerView(const Path& folder) {
                _fileSystem.Mount(MountPoints::config, folder);
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

        String SerializedText(const SceneManager& manager) {
            XmlSerializer text;
            manager.Serialize(text);
            return text.SaveToString();
        }

        /// The node a node loader gives for @p ref as text, or the loader's reason in @p error
        Optional<String> NodeText(const NodeFileLoader& loader, StringView ref, String& error) {
            pugi::xml_document document;
            if (!loader(ref, document, error)) {
                return {};
            }
            const IntrusivePtr<SceneNode> node = DeserializeNode(document.document_element(), ref);
            XmlSerializer text;
            node->Serialize(text);
            return text.SaveToString();
        }

        /// The files of one source layer and of its cooked layer, and the reading of both
        class LayerVerifier {
        public:
            LayerVerifier(const FileSystem& sourceFs, size_t layerIndex, const Path& sourceLayer, const Path& cookedLayer, const ContentCookRegistry& registry, CookReport& report)
                : _sourceFs(sourceFs), _layerIndex(layerIndex), _sourceLayer(sourceLayer), _cookedLayer(cookedLayer), _registry(registry), _report(report) {
                _sourceOnly.Mount(MountPoints::config, sourceLayer);
            }

            void Run() {
                String error;
                const Vector<Path> sourceFiles = ListContentFiles(_sourceLayer, error);
                const Vector<Path> cookedFiles = error.empty() ? ListContentFiles(_cookedLayer, error) : Vector<Path>{};
                if (!error.empty()) {
                    Fail(_report, _layerIndex, "", eastl::move(error));
                    return;
                }
                HashMap<String, bool> cookedMatched;
                bool anyBinary = false;
                for (const Path& file : cookedFiles) {
                    cookedMatched[String(file.View())] = false;
                    anyBinary = anyBinary || PathExtension(file.View()) == kBinaryExtension;
                }
                if (anyBinary) {
                    _cooked = MakeUnique<CookedLayerView>(_cookedLayer);
                }

                for (const Path& relative : sourceFiles) {
                    CookRow row;
                    row.layer = _layerIndex;
                    row.path = String(relative.View());
                    const bool isXml = PathExtension(relative.View()) == kXmlExtension;
                    const Path cookedForm = isXml ? relative.ReplaceExtension(kBinaryExtension) : relative;
                    const auto asBinary = cookedMatched.find(String(cookedForm.View()));
                    const auto asCopy = cookedMatched.find(String(relative.View()));
                    if (isXml && asBinary != cookedMatched.end()) {
                        asBinary->second = true;
                        if (asCopy != cookedMatched.end()) {
                            asCopy->second = true;
                            row.messages.push_back("the cooked layer has both the .xml and the .bin of this file: ConfigManager refuses both");
                        } else {
                            VerifyCookedFile(relative, row.messages);
                        }
                        row.outcome = row.messages.empty() ? CookOutcome::Cooked : CookOutcome::Failed;
                    } else if (asCopy != cookedMatched.end()) {
                        asCopy->second = true;
                        VerifyCopy(relative, row.messages);
                        row.outcome = row.messages.empty() ? CookOutcome::Copied : CookOutcome::Failed;
                    } else {
                        row.messages.push_back("the file is not in the cooked layer");
                        row.outcome = CookOutcome::Failed;
                    }
                    _report.rows.push_back(eastl::move(row));
                }
                for (const Path& file : cookedFiles) {
                    if (!cookedMatched[String(file.View())]) {
                        Fail(_report, _layerIndex, file.View(), "the cooked layer has this file but the source layer has nothing it is made from: the cook did not write it");
                    }
                }
            }

        private:
            void VerifyCopy(const Path& relative, Vector<String>& messages) const {
                const Optional<String> source = XmlDocument::ReadFileBytes(_sourceLayer / relative);
                const Optional<String> copy = XmlDocument::ReadFileBytes(_cookedLayer / relative);
                if (!source || !copy) {
                    messages.push_back(Format("the {} file cannot be read", source ? "cooked" : "source"));
                } else if (*source != *copy) {
                    messages.push_back(Format("the copy differs from its source ({} bytes against {})", copy->size(), source->size()));
                }
            }

            /// Compares the cooked form of the `.xml` @p relative with what it reads as; every difference is one message
            void VerifyCookedFile(const Path& relative, Vector<String>& messages) {
                const String stem(PathStem(relative.View()));
                XmlDocument document;
                if (!document.LoadFromFile(_sourceLayer / relative)) {
                    messages.push_back("the source file does not parse");
                    return;
                }
                const XmlNode root = document.GetRoot();
                const StringView rootName = root.Name();
                String sourceText;
                String cookedText;
                if (rootName == kNodeFileRootName) {
                    if (!NodesText(relative, sourceText, cookedText, messages)) {
                        return;
                    }
                } else {
                    const Path cookedFile = _cookedLayer / relative.ReplaceExtension(kBinaryExtension);
                    if (!CookedIsLoaded(stem, cookedFile, messages)) {
                        return;
                    }
                    if (rootName == kSceneConfigRootName) {
                        ScenesText(stem, root, sourceText, cookedText, messages);
                    } else if (!ConfigsText(stem, root, sourceText, cookedText, messages)) {
                        return;
                    }
                }
                if (const String difference = DescribeDifference(sourceText, cookedText); !difference.empty()) {
                    messages.push_back(Format("'{}' reads differently from the cooked file: {}", stem, difference));
                }
            }

            /// True when `ConfigManager` loaded @p cookedFile as the config @p stem; else the reason is in @p messages
            bool CookedIsLoaded(StringView stem, const Path& cookedFile, Vector<String>& messages) {
                ConfigManager& configs = _cooked->Configs();
                if (!configs.HasConfig(stem)) {
                    messages.push_back(Format("ConfigManager refuses the cooked file: {}", RefusalReason(cookedFile)));
                    return false;
                }
                if (!configs.IsBinaryConfig(stem) || !IsSameFile(configs.GetConfigFilePath(PoolString::Intern(stem)).View(), cookedFile.View()).value_or(false)) {
                    messages.push_back(Format("the stem '{}' is read from {}: another file of the cooked layer has the same stem and ConfigManager keeps that one", stem,
                                              configs.GetConfigFilePath(PoolString::Intern(stem)).View()));
                    return false;
                }
                return true;
            }

            bool NodesText(const Path& relative, String& sourceText, String& cookedText, Vector<String>& messages) const {
                const String ref(relative.ReplaceExtension("").View());
                if (!IsValidNodeRef(ref)) {
                    messages.push_back(Format("'{}' is not a valid node reference, so no scene can load this file", ref));
                    return false;
                }
                String error;
                const NodeFileLoader sourceLoader = MakeFileSystemNodeLoader(_sourceOnly);
                const Optional<String> source = NodeText(sourceLoader, ref, error);
                if (!source) {
                    messages.push_back(Format("the source node file: {}", error));
                    return false;
                }
                const NodeFileLoader cookedLoader = MakeFileSystemNodeLoader(_cooked->Files());
                const Optional<String> cooked = NodeText(cookedLoader, ref, error);
                if (!cooked) {
                    messages.push_back(Format("the cooked node file: {}", error));
                    return false;
                }
                sourceText = *source;
                cookedText = *cooked;
                return true;
            }

            void ScenesText(StringView stem, const XmlNode& root, String& sourceText, String& cookedText, Vector<String>& messages) {
                const NodeFileLoader loader = MakeFileSystemNodeLoader(_sourceFs);
                const IntrusivePtr<SceneManager> fromXml = LoadSceneManager(root, loader, stem);
                sourceText = SerializedText(*fromXml);

                const UniquePtr<IDeserializer> reader = _cooked->Configs().CreateDeserializer(stem);
                SceneLoadFindings cookedFindings;
                const IntrusivePtr<SceneManager> fromBinary = LoadSceneManagerFromBinary(*reader, stem, &cookedFindings);
                cookedText = SerializedText(*fromBinary);
                AppendFindings(cookedFindings.errors, messages);
            }

            bool ConfigsText(StringView stem, const XmlNode& root, String& sourceText, String& cookedText, Vector<String>& messages) {
                const ContentCooker* cooker = _registry.Find(stem);
                if (cooker == nullptr) {
                    messages.push_back(Format("no cooker claims the stem '{}', so the cook would not have written this file", stem));
                    return false;
                }
                XmlDeserializer xmlReader;
                if (!xmlReader.LoadFromXmlNode(root)) {
                    messages.push_back("the source document cannot be read");
                    return false;
                }
                xmlReader.SetStrictValues(cooker->strictValues);
                sourceText = cooker->describe(*cooker, xmlReader);

                const UniquePtr<IDeserializer> reader = _cooked->Configs().CreateDeserializer(stem);
                cookedText = cooker->describe(*cooker, *reader);
                return true;
            }

            const FileSystem& _sourceFs;
            size_t _layerIndex;
            Path _sourceLayer;
            Path _cookedLayer;
            const ContentCookRegistry& _registry;
            CookReport& _report;
            FileSystem _sourceOnly;
            UniquePtr<CookedLayerView> _cooked;
        };

    }  // namespace

    String DescribeDifference(StringView source, StringView cooked) {
        const Vector<StringView> a = SplitLines(source);
        const Vector<StringView> b = SplitLines(cooked);
        const size_t lines = a.size() > b.size() ? a.size() : b.size();
        size_t first = lines;
        size_t count = 0;
        for (size_t i = 0; i < lines; ++i) {
            if (i >= a.size() || i >= b.size() || a[i] != b[i]) {
                first = first == lines ? i : first;
                ++count;
            }
        }
        if (count == 0) {
            return {};
        }
        return Format("line {}: source {}, cooked {} ({} {} differ)", first + 1, QuoteLine(a, first), QuoteLine(b, first), count, count == 1 ? "line" : "lines");
    }

    CookReport VerifyCooked(const FileSystem& sourceFs, const Path& cookedDir, const ContentCookRegistry& registry) {
        CookReport report;
        const Vector<Path> layers = sourceFs.GetMountLayers(MountPoints::config);
        if (layers.empty()) {
            Fail(report, 0, "", "the file system has no 'config' layer");
            return report;
        }
        for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
            const Path cookedLayer = CookedLayerFolder(cookedDir, layerIndex);
            if (!IsDirectory(cookedLayer)) {
                Fail(report, layerIndex, "", Format("the cooked layer '{}' does not exist: run the cook first", cookedLayer.View()));
                continue;
            }
            LayerVerifier(sourceFs, layerIndex, layers[layerIndex], cookedLayer, registry, report).Run();
        }
        if (const Path extra = CookedLayerFolder(cookedDir, layers.size()); IsDirectory(extra)) {
            Fail(report, layers.size(), "", Format("'{}' is a cooked layer that has no source layer: the cook was made of other content", extra.View()));
        }
        return report;
    }

}  // namespace BECore
