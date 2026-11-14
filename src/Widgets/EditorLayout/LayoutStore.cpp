#include "LayoutStore.h"

#include <Widgets/EditorLayout/UserFileIo.h>

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

        std::filesystem::path ToPath(StringView text) {
            return std::filesystem::path(ToStdString(text));
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

    LayoutStore::LayoutStore(std::filesystem::path userDirectory) : _folder(std::move(userDirectory) / ToPath(kLayoutsFolderName)) {}

    std::filesystem::path LayoutStore::PathOf(StringView name) const {
        std::filesystem::path path = _folder / ToPath(name);
        path += ToStdString(kLayoutExtension);
        return path;
    }

    std::expected<void, LayoutError> LayoutStore::Save(StringView name, StringView text) {
        if (!IsValidLayoutName(name)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::InvalidName, Format("'{}' is not a valid layout name", name)));
        }
        if (text.empty()) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::EmptyText, "A layout with no text would not load back"));
        }
        const std::expected<void, String> written = WriteTextFileAtomically(PathOf(name), text);
        if (!written) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::WriteFailed, written.error()));
        }
        return {};
    }

    std::expected<String, LayoutError> LayoutStore::Load(StringView name) const {
        if (!IsValidLayoutName(name)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::InvalidName, Format("'{}' is not a valid layout name", name)));
        }
        const std::filesystem::path path = PathOf(name);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
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
        std::error_code ec;
        if (!std::filesystem::remove(PathOf(name), ec)) {
            if (ec) {
                return std::unexpected(MakeLayoutError(LayoutErrorKind::WriteFailed, Format("Cannot delete layout '{}': {}", name, ec.message())));
            }
            return std::unexpected(MakeLayoutError(LayoutErrorKind::NotFound, Format("There is no layout '{}'", name)));
        }
        return {};
    }

    bool LayoutStore::Exists(StringView name) const {
        if (!IsValidLayoutName(name)) {
            return false;
        }
        std::error_code ec;
        return std::filesystem::is_regular_file(PathOf(name), ec);
    }

    Vector<String> LayoutStore::List() const {
        Vector<String> names;
        std::error_code ec;
        std::filesystem::directory_iterator it(_folder, ec);
        if (ec) {
            return names;
        }
        for (const std::filesystem::directory_entry& entry : it) {
            std::error_code entryError;
            if (!entry.is_regular_file(entryError) || entry.path().extension() != ToStdString(kLayoutExtension)) {
                continue;
            }
            //-NoStd: the text of a std::filesystem::path
            const std::string stem = entry.path().stem().string();
            if (IsValidLayoutName(FromStd(stem))) {
                names.push_back(String(stem.c_str(), stem.size()));
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }

}  // namespace BECore
