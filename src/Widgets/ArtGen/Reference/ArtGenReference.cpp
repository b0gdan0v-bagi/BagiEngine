#include "ArtGenReference.h"

#include <Widgets/ArtGen/ArtGenAccept.h>

namespace BECore {

    namespace {

        constexpr StringView kModeKey = "mode";
        constexpr StringView kImageKey = "image";
        constexpr StringView kWeightKey = "weight";
        constexpr StringView kInitMode = "init";
        constexpr StringView kImageKind = "image";

        String Slashed(StringView path) {
            String result(path);
            for (char& letter : result) {
                if (letter == '\\') {
                    letter = '/';
                }
            }
            return result;
        }

        /// @p root with '/' steps and no trailing '/', so that a file inside it is `root + '/' + name`
        String SlashedRoot(StringView root) {
            String result = Slashed(root);
            while (!result.empty() && result.back() == '/') {
                result.pop_back();
            }
            return result;
        }

        /// True when a `..` step is in @p path: it would leave the folder the path was made relative to
        bool HasParentStep(StringView path) {
            size_t start = 0;
            while (start <= path.size()) {
                size_t end = path.find('/', start);
                if (end == StringView::npos) {
                    end = path.size();
                }
                if (path.substr(start, end - start) == "..") {
                    return true;
                }
                start = end + 1;
            }
            return false;
        }

    }  // namespace

    Json ArtGenReferenceToJson(const ArtGenReference& reference) {
        Json json = Json::object();
        json["mode"] = MakeJsonString(StringView(reference.mode.data(), reference.mode.size()));
        json["image"] = MakeJsonString(StringView(reference.image.data(), reference.image.size()));
        if (reference.weight) {
            json["weight"] = *reference.weight;
        }
        return json;
    }

    std::expected<ArtGenReference, String> ReadArtGenReference(const Json& value) {
        if (!value.is_object()) {
            return std::unexpected(String("'reference' must be an object"));
        }
        ArtGenReference reference;
        if (const auto mode = GetJsonString(value, kModeKey); mode && !mode->empty()) {
            reference.mode = *mode;
        } else {
            return std::unexpected(String("'reference.mode' must be a non-empty string"));
        }
        if (const auto image = GetJsonString(value, kImageKey); image && !image->empty()) {
            reference.image = *image;
        } else {
            return std::unexpected(String("'reference.image' must be a non-empty string"));
        }
        if (const Json* weight = FindJsonMember(value, kWeightKey); weight != nullptr && !weight->is_null()) {
            reference.weight = GetJsonNumber(value, kWeightKey);
            if (!reference.weight) {
                return std::unexpected(String("'reference.weight' must be a number"));
            }
        }
        return reference;
    }

    ArtGenReference ArtGenReferenceOfFile(StringView gameRoot, StringView realPath) {
        ArtGenReference reference;
        reference.mode = String(kInitMode);
        const String file = Slashed(realPath);
        const String root = SlashedRoot(gameRoot);
        const StringView fileView(file.data(), file.size());
        if (!root.empty() && fileView.size() > root.size() + 1 && fileView.substr(0, root.size()) == StringView(root.data(), root.size()) && fileView[root.size()] == '/') {
            const StringView inside = fileView.substr(root.size() + 1);
            if (!HasParentStep(inside)) {
                reference.image = String(inside);
                return reference;
            }
        }
        reference.image = file;
        return reference;
    }

    Optional<ArtGenReference> ArtGenReferenceOfVariant(StringView gameRoot, StringView kind, StringView realPath) {
        if (kind != kImageKind || realPath.empty()) {
            return std::nullopt;
        }
        return ArtGenReferenceOfFile(gameRoot, realPath);
    }

    ArtGenReference ArtGenReferenceOfSprite(StringView name) {
        ArtGenReference reference;
        reference.mode = String(kInitMode);
        reference.image = ArtGenGameFile(name, kImageKind);
        return reference;
    }

    const ArtGenReferenceModeRow& ArtGenReferenceModeOf(StringView mode) {
        for (const ArtGenReferenceModeRow& row : kArtGenReferenceModes) {
            if (row.mode == mode) {
                return row;
            }
        }
        return kArtGenReferenceModes[0];
    }

    double ArtGenReferenceDefaultWeight(StringView mode) {
        return ArtGenReferenceModeOf(mode).defaultWeight;
    }

    String ArtGenReferenceFilePath(StringView gameRoot, StringView image) {
        const String file = Slashed(image);
        const bool absolute = !file.empty() && (file[0] == '/' || (file.size() > 1 && file[1] == ':'));
        if (absolute || gameRoot.empty()) {
            return file;
        }
        const String root = SlashedRoot(gameRoot);
        return Format("{}/{}", root, file);
    }

    StringView ArtGenReferenceFileName(StringView image) {
        const size_t slash = image.find_last_of("/\\");
        return slash == StringView::npos ? image : image.substr(slash + 1);
    }

}  // namespace BECore
