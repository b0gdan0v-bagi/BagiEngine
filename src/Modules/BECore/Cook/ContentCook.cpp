#include "ContentCook.h"

#include <BECore/Config/ContentVersion.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/File/File.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Scene/SceneNodeInstance.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr StringView kXmlExtension = ".xml";
        constexpr StringView kBinaryExtension = ".bin";
        constexpr StringView kLayerFolderPrefix = "layer";
        constexpr StringView kConfigFolder = "config";
        /// The editor keeps its window layout in `config/` so that it survives a clean build; it is the developer's state, not content, and a cook must not ship it
        constexpr StringView kEditorLayoutFile = "imgui.ini";

        /// True when @p inner is @p outer or lies below it; both are compared as they exist on disk (symbolic links and `..` resolved).
        /// `Relative` is empty for equal paths and for a path outside, so the equal case is its own test; `a/bc` is not below `a/b`.
        bool IsInside(const Path& inner, const Path& outer) {
            const std::expected<String, String> innerText = WeaklyCanonicalPath(inner);
            if (!innerText) {
                return false;
            }
            const std::expected<String, String> outerText = WeaklyCanonicalPath(outer);
            if (!outerText) {
                return false;
            }
            const Path innerPath(*innerText);
            const Path outerPath(*outerText);
            return innerPath == outerPath || !innerPath.Relative(outerPath).Empty();
        }

        /// A name the cook owns in its output folder: `layer` and the digits of a layer's position
        bool IsLayerFolderName(StringView name) {
            if (name.size() <= kLayerFolderPrefix.size() || name.substr(0, kLayerFolderPrefix.size()) != kLayerFolderPrefix) {
                return false;
            }
            for (const char c : name.substr(kLayerFolderPrefix.size())) {
                if (c < '0' || c > '9') {
                    return false;
                }
            }
            return true;
        }

        void Fail(CookReport& report, size_t layer, StringView path, String message) {
            CookRow& row = report.rows.emplace_back();
            row.layer = layer;
            row.path = String(path);
            row.outcome = CookOutcome::Failed;
            row.messages.push_back(eastl::move(message));
        }

        bool IsBinaryOrXml(const Path& path) {
            const StringView extension = PathExtension(path.View());
            return extension == kXmlExtension || extension == kBinaryExtension;
        }

        /// Which of `<stem>.xml` and `<stem>.bin` a layer holds, over the whole layer as `ConfigManager` scans it
        struct StemFiles {
            bool xml = false;
            bool binary = false;
        };

        HashMap<String, StemFiles> ScanStems(const Vector<Path>& files) {
            HashMap<String, StemFiles> stems;
            for (const Path& file : files) {
                if (IsBinaryOrXml(file)) {
                    StemFiles& entry = stems[String(PathStem(file.View()))];
                    (PathExtension(file.View()) == kXmlExtension ? entry.xml : entry.binary) = true;
                }
            }
            return stems;
        }

        bool MakeParentFolder(const Path& file, String& error) {
            const Path folder = file.Parent();
            if (const std::expected<void, String> made = CreateDirectories(folder); !made) {
                error = Format("cannot make '{}': {}", folder.View(), made.error());
                return false;
            }
            return true;
        }

        /// Writes the stream of @p writer to @p target, making the folder above it; the findings get the reason it could not
        void SaveCooked(const BinarySerializer& writer, const Path& target, Vector<String>& findings) {
            String error;
            if (!MakeParentFolder(target, error)) {
                findings.push_back(eastl::move(error));
            } else if (!writer.SaveToFile(target)) {
                findings.push_back(Format("cannot write '{}'", target.View()));
            }
        }

        /// Reads @p root as the reflected type of @p cooker
        void CookWith(const ContentCooker& cooker, const XmlNode& root, BinarySerializer& writer, Vector<String>& findings) {
            XmlDeserializer reader;
            if (!reader.LoadFromXmlNode(root)) {
                findings.push_back("the document cannot be read");
                return;
            }
            reader.SetDiagnostics(DiagnosticsMode::Error);
            reader.SetStrictValues(cooker.strictValues);
            cooker.cook(cooker, reader, writer, findings);
        }

        /// The scene config as the game loads it: node references expanded (the loader sees every layer of @p fs), `sourceRef` set
        void CookScene(const FileSystem& fs, StringView stem, const XmlNode& root, BinarySerializer& writer, Vector<String>& findings) {
            if (const auto version = CheckContentVersion(root.GetPugiNode(), Format("Config '{}'", stem)); !version) {
                findings.push_back(version.error());
                return;
            }
            SceneLoadFindings loaded;
            const NodeFileLoader loader = MakeFileSystemNodeLoader(fs);
            const IntrusivePtr<SceneManager> manager = LoadSceneManager(root, loader, stem, &loaded);
            for (const String& error : loaded.expansionErrors) {
                findings.push_back(error);
            }
            AppendFindings(loaded.errors, findings);
            AppendFindings(loaded.warnings, findings);
            if (!findings.empty()) {
                return;
            }
            if (!WriteContentStamp(writer, kSceneManagerRootType)) {
                findings.push_back(Format("the type '{}' is not reflected in this program", kSceneManagerRootType));
                return;
            }
            manager->Serialize(writer);
        }

        /// A node file as `Scene::InstantiateNode` reads it: the node of the file, its children inline (a node file has no references of its own)
        void CookNode(StringView stem, const XmlNode& root, BinarySerializer& writer, Vector<String>& findings) {
            if (const auto version = CheckContentVersion(root.GetPugiNode(), Format("Config '{}'", stem)); !version) {
                findings.push_back(version.error());
                return;
            }
            if (root.GetPugiNode().attribute(kNodeRefAttribute.data())) {
                findings.push_back("the node of a node file cannot be a reference itself");
                return;
            }
            XmlDeserializer reader;
            if (!reader.LoadFromXmlNode(root)) {
                findings.push_back("the document cannot be read");
                return;
            }
            reader.SetDiagnostics(DiagnosticsMode::Error);
            reader.NoteKnownKey(kContentVersionAttribute);
            const IntrusivePtr<SceneNode> node = DeserializeNode(reader, stem);
            reader.FinishLoad();
            AppendFindings(reader, findings);
            if (!findings.empty()) {
                return;
            }
            if (!WriteContentStamp(writer, kNodeRootType)) {
                findings.push_back(Format("the type '{}' is not reflected in this program", kNodeRootType));
                return;
            }
            node->Serialize(writer);
        }

        /// Cooks the XML file @p source into @p target when its root element or its stem says how; false (nothing written, @p findings empty) when it is to be copied
        bool CookXml(const FileSystem& fs, const ContentCookRegistry& registry, const Path& source, const Path& target, Vector<String>& findings) {
            const String stem(PathStem(source.View()));
            const ContentCooker* cooker = registry.Find(stem);
            XmlDocument document;
            if (!document.LoadFromFile(source)) {
                if (cooker != nullptr) {
                    findings.push_back("the file does not parse");
                    return true;
                }
                return false;
            }
            const XmlNode root = document.GetRoot();
            const StringView rootName = root.Name();
            BinarySerializer writer;
            if (rootName == kSceneConfigRootName) {
                CookScene(fs, stem, root, writer, findings);
            } else if (rootName == kNodeFileRootName) {
                CookNode(stem, root, writer, findings);
            } else if (cooker != nullptr) {
                CookWith(*cooker, root, writer, findings);
            } else {
                return false;
            }
            if (findings.empty()) {
                SaveCooked(writer, target, findings);
            }
            return true;
        }

        void CopyOne(const Path& source, const Path& target, Vector<String>& findings) {
            String error;
            if (!MakeParentFolder(target, error)) {
                findings.push_back(eastl::move(error));
                return;
            }
            if (const std::expected<void, String> copied = CopyFileTo(source, target, CopyMode::Overwrite); !copied) {
                findings.push_back(Format("cannot copy to '{}': {}", target.View(), copied.error()));
            }
        }

        void CookLayer(const FileSystem& fs, size_t layerIndex, const Path& layer, const Path& outDir, const ContentCookRegistry& registry, CookReport& report) {
            String listError;
            const Vector<Path> files = ListContentFiles(layer, listError);
            if (!listError.empty()) {
                Fail(report, layerIndex, "", eastl::move(listError));
                return;
            }
            const HashMap<String, StemFiles> stems = ScanStems(files);
            const Path outLayer = outDir / Format("{}{}", kLayerFolderPrefix, layerIndex) / kConfigFolder;
            // A failure here shows again as the failure of the first file that needs the folder
            (void)CreateDirectories(outLayer);

            for (const Path& relative : files) {
                CookRow row;
                row.layer = layerIndex;
                row.path = String(relative.View());
                const Path source = layer / relative;

                if (IsBinaryOrXml(relative)) {
                    const StemFiles& entry = stems.find(String(PathStem(relative.View())))->second;
                    if (entry.xml && entry.binary) {
                        row.outcome = CookOutcome::Failed;
                        row.messages.push_back("a .xml and a .bin of this stem are in one layer: ConfigManager refuses both, keep one");
                        report.rows.push_back(eastl::move(row));
                        continue;
                    }
                }

                bool cooked = false;
                if (PathExtension(relative.View()) == kXmlExtension) {
                    const Path target = (outLayer / relative).ReplaceExtension(kBinaryExtension);
                    cooked = CookXml(fs, registry, source, target, row.messages);
                }
                if (!cooked) {
                    CopyOne(source, outLayer / relative, row.messages);
                }
                if (!row.messages.empty()) {
                    row.outcome = CookOutcome::Failed;
                } else {
                    row.outcome = cooked ? CookOutcome::Cooked : CookOutcome::Copied;
                }
                report.rows.push_back(eastl::move(row));
            }
        }

        /// Removes what an earlier cook left in @p outDir (`layer<k>` folders) and makes the folder; the reason when it cannot
        String PrepareOutput(const Path& outDir) {
            if (const std::expected<void, String> made = CreateDirectories(outDir); !made) {
                return Format("cannot make '{}': {}", outDir.View(), made.error());
            }
            // The folder must not change while it is walked, so the names are removed after the walk. A link named `layer<k>` counts as the folder it points at and is removed as a link
            Vector<String> stale;
            const std::expected<void, String> walked = WalkDirectory(outDir, WalkMode::Folder, [&](StringView name, FileKind kind) {
                if (kind == FileKind::Directory && IsLayerFolderName(name)) {
                    stale.emplace_back(name);
                }
                return true;
            });
            if (!walked) {
                return Format("cannot clear '{}': {}", outDir.View(), walked.error());
            }
            for (const String& name : stale) {
                if (const std::expected<void, String> removed = RemoveTree(outDir / name); !removed) {
                    return Format("cannot clear '{}': {}", outDir.View(), removed.error());
                }
            }
            return {};
        }

    }  // namespace

    Vector<Path> ListContentFiles(const Path& layer, String& error) {
        Vector<Path> files;
        const std::expected<void, String> walked = WalkDirectory(layer, WalkMode::Tree, [&](StringView relative, FileKind kind) {
            if (kind == FileKind::File) {
                const StringView name = PathFilename(relative);
                if (!name.starts_with('.') && name != kEditorLayoutFile) {
                    files.emplace_back(relative);
                }
            }
            return true;
        });
        if (!walked) {
            error = Format("the layer '{}' cannot be listed: {}", layer.View(), walked.error());
        }
        eastl::sort(files.begin(), files.end(), [](const Path& a, const Path& b) { return a.View() < b.View(); });
        return files;
    }

    bool CookReport::Ok() const {
        return Count(CookOutcome::Failed) == 0;
    }

    size_t CookReport::Count(CookOutcome outcome) const {
        size_t count = 0;
        for (const CookRow& row : rows) {
            count += row.outcome == outcome ? 1 : 0;
        }
        return count;
    }

    String CookReport::ToText() const {
        String text;
        for (const CookRow& row : rows) {
            const String where = row.path.empty() ? Format("layer{}", row.layer) : Format("layer{}/{}/{}", row.layer, kConfigFolder, row.path);
            if (row.outcome != CookOutcome::Failed) {
                text += Format("{}: {}\n", EnumUtils<CookOutcome>::ToString(row.outcome), where);
                continue;
            }
            for (const String& message : row.messages) {
                text += Format("{}: {}: {}\n", EnumUtils<CookOutcome>::ToString(row.outcome), where, message);
            }
        }
        text += Format("{} cooked, {} copied, {} failed\n", Count(CookOutcome::Cooked), Count(CookOutcome::Copied), Count(CookOutcome::Failed));
        return text;
    }

    CookReport CookContent(const FileSystem& fs, const Path& outDir, const ContentCookRegistry& registry) {
        CookReport report;
        const Vector<Path> layers = fs.GetMountLayers(MountPoints::config);
        if (layers.empty()) {
            Fail(report, 0, "", "the file system has no 'config' layer");
            return report;
        }

        Vector<Path> guarded = layers;
        for (const Path& layer : fs.GetMountLayers(MountPoints::assets)) {
            guarded.push_back(layer);
        }
        for (const Path& layer : guarded) {
            if (IsInside(outDir, layer)) {
                Fail(report, 0, "", Format("the output folder '{}' is inside the content folder '{}': it would be scanned as content", outDir.View(), layer.View()));
                return report;
            }
            if (IsInside(layer, outDir)) {
                Fail(report, 0, "", Format("the content folder '{}' is inside the output folder '{}': the cook removes what it finds there", layer.View(), outDir.View()));
                return report;
            }
        }
        if (String error = PrepareOutput(outDir); !error.empty()) {
            Fail(report, 0, "", eastl::move(error));
            return report;
        }

        for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
            CookLayer(fs, layerIndex, layers[layerIndex], outDir, registry, report);
        }
        return report;
    }

}  // namespace BECore
