#include "Path.h"

#include <BECore/File/FilePath.h>

namespace BECore {

    namespace {

        constexpr char kSeparator = '/';
        constexpr StringView kRootDirectory = "/";
        constexpr StringView kDot = ".";
        constexpr StringView kDotDot = "..";
        /// The inline names of a path that LexicallyNormal keeps without a heap block
        constexpr size_t kInlineNames = 16;
        /// The odd constant of the hash combine of boost (the golden ratio in 32 bits)
        constexpr size_t kHashCombine = 0x9e3779b9;
        constexpr size_t kHashShiftLeft = 6;
        constexpr size_t kHashShiftRight = 2;

        /// The length of the root name: a drive `C:` or the `//host` of a UNC path on Windows, nothing elsewhere
        size_t RootNameLength([[maybe_unused]] StringView text) {
#ifdef _WIN32
            const auto isAsciiLetter = [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); };
            if (text.size() >= 2 && isAsciiLetter(text[0]) && text[1] == ':') {
                return 2;
            }
            if (text.size() >= 3 && PathIsSeparator(text[0]) && PathIsSeparator(text[1]) && !PathIsSeparator(text[2])) {
                size_t end = 2;
                while (end < text.size() && !PathIsSeparator(text[end])) {
                    ++end;
                }
                return end;
            }
#endif
            return 0;
        }

        /// A path cut into its generic-format parts: root name, root directory, relative part
        struct Parts {
            size_t rootNameEnd = 0;
            bool hasRootDirectory = false;
            /// Where the names start: past the root name and the separators of the root directory
            size_t relativeBegin = 0;
        };

        Parts Split(StringView text) {
            Parts parts;
            parts.rootNameEnd = RootNameLength(text);
            parts.hasRootDirectory = parts.rootNameEnd < text.size() && PathIsSeparator(text[parts.rootNameEnd]);
            parts.relativeBegin = parts.rootNameEnd;
            if (parts.hasRootDirectory) {
                while (parts.relativeBegin < text.size() && PathIsSeparator(text[parts.relativeBegin])) {
                    ++parts.relativeBegin;
                }
            }
            return parts;
        }

        /// The names of a relative part one by one; a part that ends in a separator ends in one more, empty name, as std's iterator does
        class NameCursor {
        public:
            explicit NameCursor(StringView relative) : _text(relative) {}

            bool Next(StringView& name) {
                if (_pos >= _text.size()) {
                    name = {};
                    const bool trailing = _emitTrailing;
                    _emitTrailing = false;
                    return trailing;
                }
                const size_t start = _pos;
                while (_pos < _text.size() && !PathIsSeparator(_text[_pos])) {
                    ++_pos;
                }
                name = _text.substr(start, _pos - start);
                const size_t nameEnd = _pos;
                while (_pos < _text.size() && PathIsSeparator(_text[_pos])) {
                    ++_pos;
                }
                _emitTrailing = _pos > nameEnd && _pos >= _text.size();
                return true;
            }

        private:
            StringView _text;
            size_t _pos = 0;
            bool _emitTrailing = false;
        };

        int CompareBytes(StringView left, StringView right) {
            const size_t common = eastl::min(left.size(), right.size());
            for (size_t i = 0; i < common; ++i) {
                const auto leftByte = static_cast<unsigned char>(left[i]);
                const auto rightByte = static_cast<unsigned char>(right[i]);
                if (leftByte != rightByte) {
                    return leftByte < rightByte ? -1 : 1;
                }
            }
            if (left.size() == right.size()) {
                return 0;
            }
            return left.size() < right.size() ? -1 : 1;
        }

        size_t CombineHash(size_t seed, size_t value) {
            return seed ^ (value + kHashCombine + (seed << kHashShiftLeft) + (seed >> kHashShiftRight));
        }

        bool HasFilename(StringView text) {
            return !PathFilename(text).empty();
        }

    }  // namespace

    /// A backslash is a separator only where the OS says so (a POSIX name may hold it)
    bool PathIsSeparator(char ch) {
#ifdef _WIN32
        return ch == '/' || ch == '\\';
#else
        return ch == '/';
#endif
    }

    void PathToGenericSeparators([[maybe_unused]] String& text) {
#ifdef _WIN32
        for (char& ch : text) {
            if (ch == '\\') {
                ch = kSeparator;
            }
        }
#endif
    }

    StringView PathFilename(StringView path) {
        const Parts parts = Split(path);
        const StringView relative = path.substr(parts.relativeBegin);
        if (relative.empty() || PathIsSeparator(relative.back())) {
            return path.substr(path.size());
        }
        size_t start = relative.size();
        while (start > 0 && !PathIsSeparator(relative[start - 1])) {
            --start;
        }
        return relative.substr(start);
    }

    StringView PathParent(StringView path) {
        const Parts parts = Split(path);
        if (parts.relativeBegin >= path.size()) {
            return path;  // nothing but a root (or nothing): std returns the path itself
        }
        const size_t rootEnd = parts.rootNameEnd + (parts.hasRootDirectory ? 1 : 0);
        size_t end = path.size() - PathFilename(path).size();
        while (end > rootEnd && PathIsSeparator(path[end - 1])) {
            --end;
        }
        return path.substr(0, end);
    }

    StringView PathStem(StringView path) {
        const StringView name = PathFilename(path);
        const size_t dot = name.rfind('.');
        if (name == kDot || name == kDotDot || dot == StringView::npos || dot == 0) {
            return name;
        }
        return name.substr(0, dot);
    }

    StringView PathExtension(StringView path) {
        const StringView name = PathFilename(path);
        const size_t dot = name.rfind('.');
        if (name == kDot || name == kDotDot || dot == StringView::npos || dot == 0) {
            return path.substr(path.size());
        }
        return name.substr(dot);
    }

    bool PathIsAbsolute(StringView path) {
        const Parts parts = Split(path);
#ifdef _WIN32
        return parts.rootNameEnd > 0 && parts.hasRootDirectory;
#else
        return parts.hasRootDirectory;
#endif
    }

    StringView PathRelativeTo(StringView path, StringView base) {
        if (path.size() <= base.size() || path.substr(0, base.size()) != base) {
            return {};
        }
        size_t start = base.size();
        const bool baseEndsInSeparator = !base.empty() && PathIsSeparator(base.back());
        if (!baseEndsInSeparator && !PathIsSeparator(path[start])) {
            return {};  // "<base>x/y" is a sibling folder of the base
        }
        while (start < path.size() && PathIsSeparator(path[start])) {
            ++start;
        }
        return path.substr(start);
    }

    int PathCompare(StringView left, StringView right) {
        const Parts leftParts = Split(left);
        const Parts rightParts = Split(right);
        if (const int order = CompareBytes(left.substr(0, leftParts.rootNameEnd), right.substr(0, rightParts.rootNameEnd)); order != 0) {
            return order;
        }
        if (leftParts.hasRootDirectory != rightParts.hasRootDirectory) {
            return leftParts.hasRootDirectory ? 1 : -1;
        }
        NameCursor leftNames(left.substr(leftParts.relativeBegin));
        NameCursor rightNames(right.substr(rightParts.relativeBegin));
        for (;;) {
            StringView leftName;
            StringView rightName;
            const bool hasLeft = leftNames.Next(leftName);
            const bool hasRight = rightNames.Next(rightName);
            if (!hasLeft || !hasRight) {
                return static_cast<int>(hasLeft) - static_cast<int>(hasRight);
            }
            if (const int order = CompareBytes(leftName, rightName); order != 0) {
                return order;
            }
        }
    }

    size_t PathHash(StringView path) {
        const Parts parts = Split(path);
        size_t hash = eastl::hash<StringView>{}(path.substr(0, parts.rootNameEnd));
        hash = CombineHash(hash, parts.hasRootDirectory ? 1 : 0);
        NameCursor names(path.substr(parts.relativeBegin));
        StringView name;
        while (names.Next(name)) {
            hash = CombineHash(hash, eastl::hash<StringView>{}(name));
        }
        return hash;
    }

    Path::Path(const std::filesystem::path& path) : Path(PathToUtf8(path)) {}

    Path::operator std::filesystem::path() const {
        const auto* bytes = reinterpret_cast<const char8_t*>(_text.data());
        return std::filesystem::path(std::u8string_view(bytes, _text.size()));  //-NoStd: the bridge, UTF-8 into a path
    }

    void Path::ToGenericSeparators() {
        PathToGenericSeparators(_text);
    }

    void Path::Append(StringView other) {
        const Parts otherParts = Split(other);
        const Parts ownParts = Split(View());
        const StringView otherRoot = other.substr(0, otherParts.rootNameEnd);
        if (PathIsAbsolute(other) || (!otherRoot.empty() && otherRoot != View().substr(0, ownParts.rootNameEnd))) {
            _text.assign(other.data(), other.size());
            return;
        }
        if (otherParts.hasRootDirectory) {
            _text.resize(ownParts.rootNameEnd);  // "C:" / "/x" is "C:/x"
        } else if (HasFilename(View())) {
            _text.push_back(kSeparator);
        }
        const StringView rest = other.substr(otherParts.rootNameEnd);
        _text.append(rest.data(), rest.size());
    }

    Path& Path::operator/=(StringView other) {
#ifdef _WIN32
        Append(Path(other).View());
#else
        Append(other);
#endif
        return *this;
    }

    Path Path::ReplaceExtension(StringView extension) const {
        const StringView old = PathExtension(View());
        Path result;
        result._text.assign(_text.data(), _text.size() - old.size());
        if (!extension.empty() && extension.front() != '.') {
            result._text.push_back('.');
        }
        result._text.append(extension.data(), extension.size());
        result.ToGenericSeparators();
        return result;
    }

    Path Path::LexicallyNormal() const {
        if (_text.empty()) {
            return {};
        }
        const StringView text = View();
        const Parts parts = Split(text);
        SmallVector<StringView, kInlineNames> kept;
        // After a "." or a ".." that cancelled a name, and after a final empty name, the result ends in a separator (std: "a/b/.." is "a/")
        bool trailingSeparator = false;
        NameCursor names(text.substr(parts.relativeBegin));
        StringView name;
        while (names.Next(name)) {
            if (name.empty() || name == kDot) {
                trailingSeparator = true;
            } else if (name == kDotDot) {
                if (!kept.empty() && kept.back() != kDotDot) {
                    kept.pop_back();
                    trailingSeparator = true;
                } else if (!parts.hasRootDirectory) {
                    kept.push_back(name);
                    trailingSeparator = false;
                }  // else: ".." above the root has nothing to cancel and goes away
            } else {
                kept.push_back(name);
                trailingSeparator = false;
            }
        }

        Path result;
        result._text.assign(text.data(), parts.rootNameEnd);
        if (parts.hasRootDirectory) {
            result._text.append(kRootDirectory.data(), kRootDirectory.size());
        }
        for (size_t i = 0; i < kept.size(); ++i) {
            if (i > 0) {
                result._text.push_back(kSeparator);
            }
            result._text.append(kept[i].data(), kept[i].size());
        }
        if (trailingSeparator && !kept.empty() && kept.back() != kDotDot) {
            result._text.push_back(kSeparator);
        }
        if (result._text.empty()) {
            result._text.assign(kDot.data(), kDot.size());
        }
        result.ToGenericSeparators();
        return result;
    }

}  // namespace BECore
