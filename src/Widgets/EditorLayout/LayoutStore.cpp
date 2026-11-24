#include "LayoutStore.h"

#include <BECore/File/File.h>

namespace BECore {

    namespace {

        constexpr size_t kDeviceNumberedNameLength = 4;  // "COM1"

        char ToUpperAscii(char c) {
            return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        }

        bool IsNameChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ' ' || c == '-';
        }

        // CON, PRN, AUX, NUL, COM1..COM9, LPT1..LPT9: names that Windows opens as a device whatever the extension
        bool IsDeviceName(StringView name) {
            String upper;
            for (const char c : name) {
                upper.push_back(ToUpperAscii(c));
            }
            constexpr StringView kPlain[] = {"CON", "PRN", "AUX", "NUL"};
            for (const StringView device : kPlain) {
                if (upper == device) {
                    return true;
                }
            }
            if (upper.size() == kDeviceNumberedNameLength && upper[3] >= '1' && upper[3] <= '9') {
                return upper.compare(0, 3, "COM") == 0 || upper.compare(0, 3, "LPT") == 0;
            }
            return false;
        }

        LayoutError MakeLayoutError(LayoutErrorKind kind, String detail) {
            return LayoutError{kind, std::move(detail)};
        }

    }  // namespace

    bool IsValidLayoutName(StringView name) {
        if (name.empty() || name.size() > kMaxLayoutNameLength || name.front() == ' ' || name.back() == ' ') {
            return false;
        }
        return std::ranges::all_of(name, IsNameChar) && !IsDeviceName(name);
    }

    bool LooksLikeLayoutText(StringView text) {
        constexpr StringView kDockingSection = "[Docking][";
        return text.find(kDockingSection) != StringView::npos;
    }

    LayoutStore::LayoutStore(Path userDirectory) : _folder(userDirectory / kLayoutsFolderName) {}

    Path LayoutStore::PathOf(StringView name) const {
        return _folder / Format("{}{}", name, kLayoutExtension);
    }

    std::expected<void, LayoutError> LayoutStore::Save(StringView name, StringView text) {
        if (!IsValidLayoutName(name)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::InvalidName, Format("'{}' is not a valid layout name", name)));
        }
        if (text.empty()) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::EmptyText, "A layout with no text would not load back"));
        }
        const std::expected<void, String> written = WriteFileAtomically(PathOf(name), text);
        if (!written) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::WriteFailed, written.error()));
        }
        return {};
    }

    std::expected<String, LayoutError> LayoutStore::Load(StringView name) const {
        if (!IsValidLayoutName(name)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::InvalidName, Format("'{}' is not a valid layout name", name)));
        }
        const Path path = PathOf(name);
        if (!GetFileInfo(path)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::NotFound, Format("There is no layout '{}'", name)));
        }
        std::expected<String, String> text = ReadTextFile(path);
        if (!text) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::ReadFailed, text.error()));
        }
        return std::move(*text);
    }

    std::expected<void, LayoutError> LayoutStore::Delete(StringView name) {
        if (!IsValidLayoutName(name)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::InvalidName, Format("'{}' is not a valid layout name", name)));
        }
        const Path path = PathOf(name);
        if (!GetFileInfo(path)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::NotFound, Format("There is no layout '{}'", name)));
        }
        const std::expected<void, String> removed = RemoveFile(path);
        if (!removed) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::WriteFailed, Format("Cannot delete layout '{}': {}", name, removed.error())));
        }
        return {};
    }

    bool LayoutStore::Exists(StringView name) const {
        if (!IsValidLayoutName(name)) {
            return false;
        }
        const Optional<FileInfo> info = GetFileInfo(PathOf(name));
        return info && info->kind == FileKind::File;
    }

    Vector<String> LayoutStore::List() const {
        Vector<String> names;
        // A folder that cannot be listed is an empty list
        static_cast<void>(WalkDirectory(_folder, WalkMode::Folder, [&names](StringView relative, FileKind kind) {
            if (kind != FileKind::File || PathExtension(relative) != kLayoutExtension) {
                return true;
            }
            const StringView stem = PathStem(relative);
            if (IsValidLayoutName(stem)) {
                names.push_back(String(stem.data(), stem.size()));
            }
            return true;
        }));
        std::sort(names.begin(), names.end());
        return names;
    }

    const Vector<String>& LayoutNameCache::Names(const LayoutStore& store) {
        if (_stale) {
            _names = store.List();
            _stale = false;
        }
        return _names;
    }

}  // namespace BECore
