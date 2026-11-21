#include "ContentCook.h"

#include <BECore/Config/XmlDocument.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr StringView kXmlExtension = ".xml";
        constexpr StringView kBinaryExtension = ".bin";
        constexpr StringView kLayerFolderPrefix = "layer";
        constexpr StringView kConfigFolder = "config";

        /// True when @p inner is @p outer or lies below it; both are compared as they exist on disk (symbolic links and `..` resolved)
        bool IsInside(const std::filesystem::path& inner, const std::filesystem::path& outer) {
            std::error_code ec;
            const std::filesystem::path innerPath = std::filesystem::weakly_canonical(inner, ec);
            const std::filesystem::path outerPath = ec ? std::filesystem::path{} : std::filesystem::weakly_canonical(outer, ec);
            if (ec) {
                return false;
            }
            const std::filesystem::path relative = innerPath.lexically_relative(outerPath);
            return !relative.empty() && *relative.begin() != "..";
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

        bool IsBinaryOrXml(const std::filesystem::path& path) {
            const std::filesystem::path extension = path.extension();
            return extension == kXmlExtension.data() || extension == kBinaryExtension.data();
        }

        /// The files of @p layer relative to it, sorted; dot files (`.DS_Store`) are not content
        Vector<std::filesystem::path> ListFiles(const std::filesystem::path& layer, String& error) {
            Vector<std::filesystem::path> files;
            std::error_code ec;
            for (std::filesystem::recursive_directory_iterator it(layer, ec), end; !ec && it != end; it.increment(ec)) {  //-NoStd: an offline tool lists the content folder once
                if (it->is_regular_file(ec) && !ec && it->path().filename().string().front() != '.') {
                    files.push_back(it->path().lexically_relative(layer));
                }
            }
            if (ec) {
                error = Format("the layer '{}' cannot be listed: {}", layer.string(), ec.message());
            }
            eastl::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) { return a.generic_string() < b.generic_string(); });
            return files;
        }

        /// Which of `<stem>.xml` and `<stem>.bin` a layer holds, over the whole layer as `ConfigManager` scans it
        struct StemFiles {
            bool xml = false;
            bool binary = false;
        };

        HashMap<String, StemFiles> ScanStems(const Vector<std::filesystem::path>& files) {
            HashMap<String, StemFiles> stems;
            for (const std::filesystem::path& file : files) {
                if (IsBinaryOrXml(file)) {
                    StemFiles& entry = stems[FromStdString(file.stem().string())];
                    (file.extension() == kXmlExtension.data() ? entry.xml : entry.binary) = true;
                }
            }
            return stems;
        }

        bool MakeParentFolder(const std::filesystem::path& file, String& error) {
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
            if (ec) {
                error = Format("cannot make '{}': {}", file.parent_path().string(), ec.message());
            }
            return !ec;
        }

        /// Reads @p source as the cooker says and writes @p target; the findings are the reason it did not
        void CookOne(const ContentCooker& cooker, const std::filesystem::path& source, const std::filesystem::path& target, Vector<String>& findings) {
            XmlDocument document;
            if (!document.LoadFromFile(source)) {
                findings.push_back("the file does not parse");
                return;
            }
            XmlDeserializer reader;
            if (!reader.LoadFromXmlNode(document.GetRoot())) {
                findings.push_back("the document cannot be read");
                return;
            }
            reader.SetDiagnostics(DiagnosticsMode::Error);
            reader.SetStrictValues(cooker.strictValues);

            BinarySerializer writer;
            cooker.cook(cooker, reader, writer, findings);
            if (!findings.empty()) {
                return;
            }
            String error;
            if (!MakeParentFolder(target, error)) {
                findings.push_back(eastl::move(error));
            } else if (!writer.SaveToFile(target)) {
                findings.push_back(Format("cannot write '{}'", target.string()));
            }
        }

        void CopyOne(const std::filesystem::path& source, const std::filesystem::path& target, Vector<String>& findings) {
            String error;
            if (!MakeParentFolder(target, error)) {
                findings.push_back(eastl::move(error));
                return;
            }
            std::error_code ec;
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                findings.push_back(Format("cannot copy to '{}': {}", target.string(), ec.message()));
            }
        }

        void CookLayer(size_t layerIndex, const std::filesystem::path& layer, const std::filesystem::path& outDir, const ContentCookRegistry& registry, CookReport& report) {
            String listError;
            const Vector<std::filesystem::path> files = ListFiles(layer, listError);
            if (!listError.empty()) {
                Fail(report, layerIndex, "", eastl::move(listError));
                return;
            }
            const HashMap<String, StemFiles> stems = ScanStems(files);
            const std::filesystem::path outLayer = outDir / Format("{}{}", kLayerFolderPrefix, layerIndex).c_str() / kConfigFolder.data();
            std::error_code ec;
            std::filesystem::create_directories(outLayer, ec);

            for (const std::filesystem::path& relative : files) {
                CookRow row;
                row.layer = layerIndex;
                row.path = Generic(relative);
                const std::filesystem::path source = layer / relative;

                if (IsBinaryOrXml(relative)) {
                    const StemFiles& entry = stems.find(FromStdString(relative.stem().string()))->second;
                    if (entry.xml && entry.binary) {
                        row.outcome = CookOutcome::Failed;
                        row.messages.push_back("a .xml and a .bin of this stem are in one layer: ConfigManager refuses both, keep one");
                        report.rows.push_back(eastl::move(row));
                        continue;
                    }
                }

                const ContentCooker* cooker = relative.extension() == kXmlExtension.data() ? registry.Find(FromStdString(relative.stem().string())) : nullptr;
                if (cooker != nullptr) {
                    std::filesystem::path target = outLayer / relative;
                    target.replace_extension(kBinaryExtension.data());
                    CookOne(*cooker, source, target, row.messages);
                    row.outcome = row.messages.empty() ? CookOutcome::Cooked : CookOutcome::Failed;
                } else {
                    CopyOne(source, outLayer / relative, row.messages);
                    row.outcome = row.messages.empty() ? CookOutcome::Copied : CookOutcome::Failed;
                }
                report.rows.push_back(eastl::move(row));
            }
        }

        /// Removes what an earlier cook left in @p outDir (`layer<k>` folders) and makes the folder; the reason when it cannot
        String PrepareOutput(const std::filesystem::path& outDir) {
            std::error_code ec;
            std::filesystem::create_directories(outDir, ec);
            if (ec) {
                return Format("cannot make '{}': {}", outDir.string(), ec.message());
            }
            for (std::filesystem::directory_iterator it(outDir, ec), end; !ec && it != end; it.increment(ec)) {  //-NoStd: an offline tool lists the output folder once
                if (it->is_directory(ec) && IsLayerFolderName(FromStdString(it->path().filename().string()))) {
                    std::filesystem::remove_all(it->path(), ec);
                }
            }
            return ec ? Format("cannot clear '{}': {}", outDir.string(), ec.message()) : String{};
        }

    }  // namespace

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

    CookReport CookContent(const FileSystem& fs, const std::filesystem::path& outDir, const ContentCookRegistry& registry) {
        CookReport report;
        const Vector<std::filesystem::path> layers = fs.GetMountLayers(MountPoints::config);
        if (layers.empty()) {
            Fail(report, 0, "", "the file system has no 'config' layer");
            return report;
        }

        Vector<std::filesystem::path> guarded = layers;
        for (const std::filesystem::path& layer : fs.GetMountLayers(MountPoints::assets)) {
            guarded.push_back(layer);
        }
        for (const std::filesystem::path& layer : guarded) {
            if (IsInside(outDir, layer)) {
                Fail(report, 0, "", Format("the output folder '{}' is inside the content folder '{}': it would be scanned as content", outDir.string(), layer.string()));
                return report;
            }
            if (IsInside(layer, outDir)) {
                Fail(report, 0, "", Format("the content folder '{}' is inside the output folder '{}': the cook removes what it finds there", layer.string(), outDir.string()));
                return report;
            }
        }
        if (String error = PrepareOutput(outDir); !error.empty()) {
            Fail(report, 0, "", eastl::move(error));
            return report;
        }

        for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
            CookLayer(layerIndex, layers[layerIndex], outDir, registry, report);
        }
        return report;
    }

}  // namespace BECore
