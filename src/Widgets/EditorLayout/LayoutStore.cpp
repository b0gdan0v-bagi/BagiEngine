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
        bool IsDeviceName(eastl::string_view name) {
            eastl::string upper;
            for (const char c : name) {
                upper.push_back(ToUpperAscii(c));
            }
            constexpr eastl::string_view kPlain[] = {"CON", "PRN", "AUX", "NUL"};
            for (const eastl::string_view device : kPlain) {
                if (upper == device) {
                    return true;
                }
            }
            if (upper.size() == kDeviceNumberedNameLength && upper[3] >= '1' && upper[3] <= '9') {
                return upper.compare(0, 3, "COM") == 0 || upper.compare(0, 3, "LPT") == 0;
            }
            return false;
        }

        LayoutError MakeLayoutError(LayoutErrorKind kind, eastl::string detail) {
            return LayoutError{kind, std::move(detail)};
        }

        std::filesystem::path ToPath(eastl::string_view text) {
            return std::filesystem::path(std::string(text.data(), text.size()));
        }

    }  // namespace

    bool IsValidLayoutName(eastl::string_view name) {
        if (name.empty() || name.size() > kMaxLayoutNameLength || name.front() == ' ' || name.back() == ' ') {
            return false;
        }
        return std::ranges::all_of(name, IsNameChar) && !IsDeviceName(name);
    }

    bool LooksLikeLayoutText(eastl::string_view text) {
        constexpr eastl::string_view kDockingSection = "[Docking][";
        return text.find(kDockingSection) != eastl::string_view::npos;
    }

    LayoutStore::LayoutStore(std::filesystem::path userDirectory) : _folder(std::move(userDirectory) / ToPath(kLayoutsFolderName)) {}

    std::filesystem::path LayoutStore::PathOf(eastl::string_view name) const {
        std::filesystem::path path = _folder / ToPath(name);
        path += std::string(kLayoutExtension.data(), kLayoutExtension.size());
        return path;
    }

    std::expected<void, LayoutError> LayoutStore::Save(eastl::string_view name, eastl::string_view text) {
        if (!IsValidLayoutName(name)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::InvalidName, Format("'{}' is not a valid layout name", name)));
        }
        if (text.empty()) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::EmptyText, "A layout with no text would not load back"));
        }
        const std::expected<void, eastl::string> written = WriteTextFileAtomically(PathOf(name), text);
        if (!written) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::WriteFailed, written.error()));
        }
        return {};
    }

    std::expected<eastl::string, LayoutError> LayoutStore::Load(eastl::string_view name) const {
        if (!IsValidLayoutName(name)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::InvalidName, Format("'{}' is not a valid layout name", name)));
        }
        const std::filesystem::path path = PathOf(name);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::NotFound, Format("There is no layout '{}'", name)));
        }
        std::expected<eastl::string, eastl::string> text = ReadTextFile(path);
        if (!text) {
            return std::unexpected(MakeLayoutError(LayoutErrorKind::ReadFailed, text.error()));
        }
        return std::move(*text);
    }

    std::expected<void, LayoutError> LayoutStore::Delete(eastl::string_view name) {
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

    bool LayoutStore::Exists(eastl::string_view name) const {
        if (!IsValidLayoutName(name)) {
            return false;
        }
        std::error_code ec;
        return std::filesystem::is_regular_file(PathOf(name), ec);
    }

    eastl::vector<eastl::string> LayoutStore::List() const {
        eastl::vector<eastl::string> names;
        std::error_code ec;
        std::filesystem::directory_iterator it(_folder, ec);
        if (ec) {
            return names;
        }
        for (const std::filesystem::directory_entry& entry : it) {
            std::error_code entryError;
            if (!entry.is_regular_file(entryError) || entry.path().extension() != std::string(kLayoutExtension.data(), kLayoutExtension.size())) {
                continue;
            }
            const std::string stem = entry.path().stem().string();
            if (IsValidLayoutName(eastl::string_view(stem.data(), stem.size()))) {
                names.push_back(eastl::string(stem.c_str(), stem.size()));
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }

}  // namespace BECore
