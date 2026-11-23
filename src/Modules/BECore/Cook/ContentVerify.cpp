#include "ContentVerify.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ContentVersion.h>
#include <BECore/Config/XmlDocument.h>
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

        String Generic(const std::filesystem::path& path) {
            return FromStdString(path.generic_string());
        }

        void Fail(CookReport& report, size_t layer, StringView path, String message) {
            CookRow& row = report.rows.emplace_back();
            row.layer = layer;
            row.path = String(path);
            row.outcome = CookOutcome::Failed;
            row.messages.push_back(eastl::move(message));
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

        std::filesystem::path CookedLayerFolder(const std::filesystem::path& cookedDir, size_t layer) {
            return cookedDir / Format("{}{}", kLayerFolderPrefix, layer).c_str() / kConfigFolder.data();
        }

        std::filesystem::path WithExtension(std::filesystem::path path, StringView extension) {
            return path.replace_extension(extension.data());
        }

        /// The reason a `.bin` is refused: the checks `ConfigManager` makes of a file it loads, with the message of the first that fails
        String RefusalReason(const std::filesystem::path& path) {
            Vector<uint8_t> bytes;
            if (const std::expected<void, StringView> read = ReadBinaryDocumentFile(path, bytes); !read.has_value()) {
                return String(read.error());
            }
            BinaryDeserializer reader;
            if (!reader.LoadFromView(bytes.data(), bytes.size())) {
                const auto& errors = reader.GetErrors();
                return errors.empty() ? String("the file is not a binary document") : errors.front().errorMessage;
            }
            const std::string source = path.string();  //-NoStd: the path API's own text
            if (const auto stamp = CheckContentStamp(reader, StringView(source.data(), source.size())); !stamp) {
                return stamp.error();
            }
            return "ConfigManager did not load the file";
        }

        /// What `ConfigManager` makes of the cooked layer: a file system of its own over that one folder, so a file is read the way the game reads a layer
        struct CookedLayerView {
            explicit CookedLayerView(const std::filesystem::path& folder) {
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
            LayerVerifier(const FileSystem& sourceFs, size_t layerIndex, const std::filesystem::path& sourceLayer, const std::filesystem::path& cookedLayer, const ContentCookRegistry& registry,
                          CookReport& report)
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
                for (const std::filesystem::path& file : cookedFiles) {
                    cookedMatched[Generic(file)] = false;
                    anyBinary = anyBinary || file.extension() == kBinaryExtension.data();
                }
                if (anyBinary) {
                    _cooked = MakeUnique<CookedLayerView>(_cookedLayer);
                }

                for (const std::filesystem::path& relative : sourceFiles) {
                    CookRow row;
                    row.layer = _layerIndex;
                    row.path = Generic(relative);
                    const bool isXml = relative.extension() == kXmlExtension.data();
                    const std::filesystem::path cookedForm = isXml ? WithExtension(relative, kBinaryExtension) : relative;
                    const auto asBinary = cookedMatched.find(Generic(cookedForm));
                    const auto asCopy = cookedMatched.find(Generic(relative));
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
                for (const std::filesystem::path& file : cookedFiles) {
                    if (!cookedMatched[Generic(file)]) {
                        Fail(_report, _layerIndex, Generic(file), "the cooked layer has this file but the source layer has nothing it is made from: the cook did not write it");
                    }
                }
            }

        private:
            void VerifyCopy(const std::filesystem::path& relative, Vector<String>& messages) const {
                const Optional<String> source = XmlDocument::ReadFileBytes(_sourceLayer / relative);
                const Optional<String> copy = XmlDocument::ReadFileBytes(_cookedLayer / relative);
                if (!source || !copy) {
                    messages.push_back(Format("the {} file cannot be read", source ? "cooked" : "source"));
                } else if (*source != *copy) {
                    messages.push_back(Format("the copy differs from its source ({} bytes against {})", copy->size(), source->size()));
                }
            }

            /// Compares the cooked form of the `.xml` @p relative with what it reads as; every difference is one message
            void VerifyCookedFile(const std::filesystem::path& relative, Vector<String>& messages) {
                const String stem = FromStdString(relative.stem().string());
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
                    const std::filesystem::path cookedFile = _cookedLayer / WithExtension(relative, kBinaryExtension);
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
            bool CookedIsLoaded(StringView stem, const std::filesystem::path& cookedFile, Vector<String>& messages) {
                ConfigManager& configs = _cooked->Configs();
                if (!configs.HasConfig(stem)) {
                    messages.push_back(Format("ConfigManager refuses the cooked file: {}", RefusalReason(cookedFile)));
                    return false;
                }
                std::error_code ec;
                if (!configs.IsBinaryConfig(stem) || !std::filesystem::equivalent(configs.GetConfigFilePath(PoolString::Intern(stem)), cookedFile, ec)) {
                    messages.push_back(Format("the stem '{}' is read from {}: another file of the cooked layer has the same stem and ConfigManager keeps that one", stem,
                                              configs.GetConfigFilePath(PoolString::Intern(stem)).View()));
                    return false;
                }
                return true;
            }

            bool NodesText(const std::filesystem::path& relative, String& sourceText, String& cookedText, Vector<String>& messages) const {
                const String ref = Generic(WithExtension(relative, ""));
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
            std::filesystem::path _sourceLayer;
            std::filesystem::path _cookedLayer;
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

    CookReport VerifyCooked(const FileSystem& sourceFs, const std::filesystem::path& cookedDir, const ContentCookRegistry& registry) {
        CookReport report;
        const Vector<Path> layers = sourceFs.GetMountLayers(MountPoints::config);
        if (layers.empty()) {
            Fail(report, 0, "", "the file system has no 'config' layer");
            return report;
        }
        for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
            const std::filesystem::path cookedLayer = CookedLayerFolder(cookedDir, layerIndex);
            std::error_code ec;
            if (!std::filesystem::is_directory(cookedLayer, ec)) {
                Fail(report, layerIndex, "", Format("the cooked layer '{}' does not exist: run the cook first", cookedLayer.string()));
                continue;
            }
            LayerVerifier(sourceFs, layerIndex, layers[layerIndex], cookedLayer, registry, report).Run();
        }
        std::error_code ec;
        if (const std::filesystem::path extra = CookedLayerFolder(cookedDir, layers.size()); std::filesystem::is_directory(extra, ec)) {
            Fail(report, layers.size(), "", Format("'{}' is a cooked layer that has no source layer: the cook was made of other content", extra.string()));
        }
        return report;
    }

}  // namespace BECore
