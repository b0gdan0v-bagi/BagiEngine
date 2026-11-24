#pragma once

#include <BECore/Types/Types.h>
#include <compare>
#include <filesystem>  //-NoStd: transitional bridge of the entry engine-path-type, removed in its last stage
#include <type_traits>

namespace BECore {

    /**
     * The path helpers below work on UTF-8 text and follow `std::filesystem::path` (the generic format) component for component, so the code
     * that used it keeps its results:
     * - `PathFilename("a/b/")` is empty (the path ends in a separator), `PathFilename("/")` too.
     * - `PathStem(".hidden")` is `.hidden` and `PathExtension(".hidden")` is empty: a dot at the start of a name is no extension. `.` and
     *   `..` have no extension either.
     * - `PathExtension("a.b.c")` is `.c` (with the dot), `PathStem("a.b.c")` is `a.b`; `PathExtension("a.")` is `.`.
     * - `PathParent("a")` is empty, `PathParent("/a")` is `/`, `PathParent("/")` is `/`, `PathParent("a/b/")` is `a/b`.
     * - A run of separators at the start is one root directory: `PathParent("//a")` is `/` (libc++ keeps `//`; POSIX leaves it open).
     * A separator is `/`; a backslash is one only on Windows. Nothing here allocates: the results are views into the argument.
     */
    [[nodiscard]] StringView PathParent(StringView path);
    [[nodiscard]] StringView PathFilename(StringView path);
    [[nodiscard]] StringView PathStem(StringView path);
    [[nodiscard]] StringView PathExtension(StringView path);

    /// A separator of `std::filesystem::path`: `/`, and a backslash only on Windows.
    [[nodiscard]] bool PathIsSeparator(char ch);

    /// On Windows each `\` of @p text becomes `/`, as `Path`'s constructor does; elsewhere the text stays.
    void PathToGenericSeparators(String& text);

    /// A drive path `C:/a` or a UNC path counts on Windows; on the other systems the text starts with `/`.
    [[nodiscard]] bool PathIsAbsolute(StringView path);

    /**
     * The part of @p path below @p base, or an empty view when @p path does not start with @p base as a folder (`a/bc` is not below `a/b`)
     * or is not longer than it. The paths are compared as text: both must be in the same form (see `Path::LexicallyNormal`).
     */
    [[nodiscard]] StringView PathRelativeTo(StringView path, StringView base);

    /**
     * Whether @p path is @p root or lies below it, name by name (`/a/bc` is not under `/a/b`); a trailing separator on either side does not
     * matter, and an empty @p root matches nothing. Like `PathRelativeTo`, both must be in the same form (see `Path::LexicallyNormal`).
     */
    [[nodiscard]] bool PathIsUnder(StringView path, StringView root);

    /**
     * The order of `std::filesystem::path::compare`: the root name, then a root directory after none, then the names one by one, each byte by
     * byte as unsigned values. `a/b` comes before `a-b` and `a.txt`, which a comparison of the whole text would put after them, and `a//b`
     * equals `a/b`. A trailing separator is a last empty name: `a` comes before `a/`. Negative, zero or positive.
     */
    [[nodiscard]] int PathCompare(StringView left, StringView right);

    /// The hash of the same components that `PathCompare` compares: the paths it calls equal hash alike.
    [[nodiscard]] size_t PathHash(StringView path);

    /**
     * A file path as UTF-8 text with `/` separators on every system: on Windows the constructor turns each `\` into `/` (the system takes
     * both; this is the text `generic_string()` gave), elsewhere the text is stored as given (a backslash is a name character there).
     *
     * The text stays as written: `a//b` and `a/./b` are kept, and `operator==`, `operator<=>` and the hash see through them by components
     * (`PathCompare`). A path is a real one, never a virtual `mount:/...` (`FileSystem::ResolvePath` first); `Path` knows no mounts.
     * Members that return a `Path` copy what the view helpers above return, and `LexicallyNormal` is `std::filesystem::lexically_normal`
     * (`.` and `..` folded, separators merged, a trailing separator kept, an empty result is `.`).
     */
    class Path {
    public:
        Path() = default;
        Path(StringView text) : _text(text.data(), text.size()) {
            ToGenericSeparators();
        }
        Path(const char* text) : Path(StringView(text)) {}
        Path(const String& text) : _text(text) {
            ToGenericSeparators();
        }
        Path(String&& text) : _text(eastl::move(text)) {
            ToGenericSeparators();
        }

        /// The only place where the two path types meet: the bridge of the migration to `Path`, which its last stage deletes. UTF-8 both ways.
        Path(const std::filesystem::path& path);  //-NoStd: transitional bridge
        operator std::filesystem::path() const;   //-NoStd: transitional bridge

        [[nodiscard]] StringView View() const {
            return StringView(_text.data(), _text.size());
        }
        operator StringView() const {
            return View();
        }
        /// The text NUL-terminated at `View().size()`, for a C API that takes UTF-8 `char*` (the engine `String` always ends in a zero)
        [[nodiscard]] const char* CStr() const {
            return _text.c_str();
        }

        [[nodiscard]] bool Empty() const {
            return _text.empty();
        }
        void Clear() {
            _text.clear();
        }

        [[nodiscard]] Path Parent() const {
            return Path(PathParent(View()));
        }
        [[nodiscard]] Path Filename() const {
            return Path(PathFilename(View()));
        }
        [[nodiscard]] Path Stem() const {
            return Path(PathStem(View()));
        }
        [[nodiscard]] Path Extension() const {
            return Path(PathExtension(View()));
        }
        [[nodiscard]] bool IsAbsolute() const {
            return PathIsAbsolute(View());
        }
        [[nodiscard]] Path Relative(const Path& base) const {
            return Path(PathRelativeTo(View(), base.View()));
        }
        /// This path is @p root or lies below it (`PathIsUnder`)
        [[nodiscard]] bool IsUnder(const Path& root) const {
            return PathIsUnder(View(), root.View());
        }

        /// The path with its extension replaced by @p extension (a dot is put in front when it has none, an empty one removes it)
        [[nodiscard]] Path ReplaceExtension(StringView extension) const;

        [[nodiscard]] Path LexicallyNormal() const;

        /**
         * Joins as `std::filesystem::path::operator/=` does: one `/` between a left side that ends in a name and the right side, none after
         * a separator or for an empty left side; an absolute right side replaces the left. The right side of any other type goes through
         * `Path`'s constructor (a template, so no two conversions compete for a `const char*`).
         */
        Path& operator/=(const Path& other) {
            if (&other == this) {
                const Path copy = other;  // Append grows _text, which the right side views
                Append(copy.View());
            } else {
                Append(other.View());
            }
            return *this;
        }
        Path& operator/=(StringView other);
        template <typename Other>
        requires(!std::is_same_v<Other, Path> && !std::is_same_v<Other, StringView> && std::is_constructible_v<Path, const Other&>) Path& operator/=(const Other& other) {
            return *this /= Path(other);
        }

        [[nodiscard]] Path operator/(const Path& other) const {
            Path joined = *this;
            joined /= other;
            return joined;
        }
        [[nodiscard]] Path operator/(StringView other) const {
            Path joined = *this;
            joined /= other;
            return joined;
        }
        template <typename Other>
        requires(!std::is_same_v<Other, Path> && !std::is_same_v<Other, StringView> && std::is_constructible_v<Path, const Other&>) [[nodiscard]] Path operator/(const Other& other) const {
            return *this / Path(other);
        }

        [[nodiscard]] bool operator==(const Path& other) const {
            return _text == other._text || PathCompare(View(), other.View()) == 0;
        }
        // clang-format off: the project's clang-format version splits the three-way comparison operator into `<= >`
        [[nodiscard]] std::strong_ordering operator<=>(const Path& other) const {
            const int order = PathCompare(View(), other.View());
            return order < 0 ? std::strong_ordering::less : (order > 0 ? std::strong_ordering::greater : std::strong_ordering::equal);
        }
        // clang-format on

    private:
        void ToGenericSeparators();
        void Append(StringView other);

        String _text;
    };

}  // namespace BECore

namespace eastl {

    /// So `HashMap<Path, V>` works; equal by components, like `operator==`
    template <>
    struct hash<BECore::Path> {
        size_t operator()(const BECore::Path& path) const noexcept {
            return BECore::PathHash(path.View());
        }
    };

}  // namespace eastl
