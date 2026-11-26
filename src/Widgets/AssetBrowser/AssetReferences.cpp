#include "AssetReferences.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Widgets/AssetBrowser/AssetCatalog.h>
#include <Widgets/AssetBrowser/AssetFileOps.h>

namespace BECore {

    namespace {

        /// How the text of a source file is read
        CORE_ENUM(SourceSyntax, uint8_t, Markup, Style, Json)

        struct SourceExtension {
            StringView extension;
            SourceSyntax syntax = SourceSyntax::Markup;
            /// A link of such a file may start at the file's own folder
            bool linksFromFolder = false;
        };

        constexpr SourceExtension kSourceExtensions[] = {
            {".xml", SourceSyntax::Markup, false}, {".html", SourceSyntax::Markup, true}, {".rml", SourceSyntax::Markup, true},
            {".css", SourceSyntax::Style, true},   {".rcss", SourceSyntax::Style, true},  {".json", SourceSyntax::Json, false},
        };

        /// The Generate panel's presets and sets, below the content root and in no mount point
        constexpr StringView kArtGenFolder = "artgen";

        /// What ends a word that may be a link
        constexpr StringView kWordDelimiters = " \t\r\"'`()<>=,;{}[]|";

        /// A name with one of these is a pattern that stands for names
        constexpr StringView kPatternCharacters = "*%$";

        /// A quoted text with one of these is a template, a list or a sentence and not one name with spaces
        constexpr StringView kNotInAValue = "<>{}|;=,\"*%$";

        /// What may follow a name that ends a sentence
        constexpr StringView kSentenceEnd = ".:!?";

        /// The query and the fragment of a link
        constexpr StringView kLinkSuffixStart = "?#";

        constexpr StringView kUtf8ByteOrderMark = "\xEF\xBB\xBF";
        constexpr StringView kStyleElementOpen = "<style";
        constexpr StringView kStyleElementClose = "</style";

        struct NamedEntity {
            StringView name;
            char character = '\0';
        };

        constexpr NamedEntity kXmlEntities[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};

        constexpr uint32_t kMaxCodePoint = 0x10FFFF;
        constexpr uint32_t kHighSurrogateFirst = 0xD800;
        constexpr uint32_t kLowSurrogateFirst = 0xDC00;
        constexpr uint32_t kSurrogateEnd = 0xE000;
        constexpr uint32_t kSupplementaryFirst = 0x10000;
        constexpr uint32_t kSurrogateBits = 10;
        constexpr StringView kMarkupCommentOpen = "<!--";
        constexpr StringView kMarkupCommentClose = "-->";
        constexpr StringView kCdataOpen = "<![CDATA[";
        constexpr StringView kCdataClose = "]]>";
        constexpr StringView kStyleCommentOpen = "/*";
        constexpr StringView kStyleCommentClose = "*/";

        constexpr StringView kNumericReference = "&#";
        constexpr StringView kHexReference = "&#x";
        constexpr StringView kJsonUnicodeEscape = "\\u";
        constexpr size_t kJsonUnicodeDigits = 4;
        constexpr size_t kJsonUnicodeLength = kJsonUnicodeEscape.size() + kJsonUnicodeDigits;

        /// What the check looks for; every text in lower case, as the sources are searched
        struct Target {
            String path;
            String fileName;
            /// The reference a scene names a node file by; empty for a file that can have none
            String nodeRef;
        };

        char ToLowerAscii(char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        String ToLowerAscii(StringView text) {
            String lower(text);
            for (char& c : lower) {
                c = ToLowerAscii(c);
            }
            return lower;
        }

        bool IsSpace(char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        /// A character that makes the text next to it part of a longer name; every byte of a non-ASCII character is one
        bool IsNameCharacter(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || static_cast<uint8_t>(c) >= 0x80;
        }

        /// Whether `[begin, end)` of @p text stands on its own, so it is not a piece of a longer path
        bool IsDelimited(StringView text, size_t begin, size_t end) {
            if (begin > 0) {
                const char before = text[begin - 1];
                if (IsNameCharacter(before) || before == '.' || before == '/') {
                    return false;
                }
            }
            if (end >= text.size()) {
                return true;
            }
            const char after = text[end];
            if (IsNameCharacter(after) || after == '/') {
                return false;
            }
            // A dot ends a sentence, or starts one more extension
            return after != '.' || end + 1 >= text.size() || !IsNameCharacter(text[end + 1]);
        }

        void Blank(String& text, size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (text[i] != '\n') {
                    text[i] = ' ';
                }
            }
        }

        /// Blanks `<!-- -->` where it is a comment: not inside a tag's quoted value or a CDATA section. An unclosed one stays
        void BlankMarkupComments(String& text) {
            bool inTag = false;
            char quote = '\0';
            for (size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (quote != '\0') {
                    if (c == quote) {
                        quote = '\0';
                    }
                    continue;
                }
                if (inTag) {
                    if (c == '"' || c == '\'') {
                        quote = c;
                    } else if (c == '>') {
                        inTag = false;
                    }
                    continue;
                }
                if (c != '<' || i + 1 >= text.size()) {
                    continue;
                }
                const StringView rest = StringView(text).substr(i);
                if (rest.starts_with(kMarkupCommentOpen)) {
                    const size_t close = StringView(text).find(kMarkupCommentClose, i + kMarkupCommentOpen.size());
                    if (close != StringView::npos) {
                        Blank(text, i, close + kMarkupCommentClose.size());
                        i = close + kMarkupCommentClose.size() - 1;
                    }
                } else if (rest.starts_with(kCdataOpen)) {
                    const size_t close = StringView(text).find(kCdataClose, i);
                    if (close == StringView::npos) {
                        return;
                    }
                    i = close + kCdataClose.size() - 1;
                } else {
                    const char next = text[i + 1];
                    inTag = (next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || next == '/' || next == '!' || next == '?';
                }
            }
        }

        /// Blanks the comments of the style sheet in `[begin, end)` of @p text that are not inside a string. An unclosed one stays
        void BlankStyleComments(String& text, size_t begin, size_t end) {
            char quote = '\0';
            for (size_t i = begin; i < end; ++i) {
                const char c = text[i];
                if (quote != '\0') {
                    if (c == '\\') {
                        ++i;
                    } else if (c == quote || c == '\n') {
                        quote = '\0';
                    }
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (StringView(text).substr(i).starts_with(kStyleCommentOpen)) {
                    const size_t close = StringView(text).find(kStyleCommentClose, i + kStyleCommentOpen.size());
                    if (close != StringView::npos && close + kStyleCommentClose.size() <= end) {
                        Blank(text, i, close + kStyleCommentClose.size());
                        i = close + kStyleCommentClose.size() - 1;
                    }
                }
            }
        }

        /// Blanks the comments of the style elements of a document, which are no comments of its markup
        void BlankEmbeddedStyleComments(String& text) {
            const String lower = ToLowerAscii(StringView(text));
            for (size_t at = lower.find(kStyleElementOpen.data()); at != String::npos;) {
                const size_t open = lower.find('>', at);
                if (open == String::npos) {
                    return;
                }
                const size_t close = lower.find(kStyleElementClose.data(), open);
                const size_t end = close == String::npos ? lower.size() : close;
                BlankStyleComments(text, open + 1, end);
                at = lower.find(kStyleElementOpen.data(), end);
            }
        }

        /// Appends the UTF-8 of @p codePoint; a line break becomes a space, so a decoded text keeps its lines
        void AppendCharacter(String& out, uint32_t codePoint) {
            if (codePoint == '\n' || codePoint == '\r') {
                out.push_back(' ');
            } else if (codePoint < 0x80) {
                out.push_back(static_cast<char>(codePoint));
            } else if (codePoint < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            } else if (codePoint < kSupplementaryFirst) {
                out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
        }

        /// The number all of @p digits write in @p base (10 or 16), when it is a code point
        Optional<uint32_t> ParseCodePoint(StringView digits, uint32_t base) {
            if (digits.empty()) {
                return std::nullopt;
            }
            uint32_t value = 0;
            for (const char c : digits) {
                const char lower = ToLowerAscii(c);
                uint32_t digit = base;
                if (lower >= '0' && lower <= '9') {
                    digit = static_cast<uint32_t>(lower - '0');
                } else if (lower >= 'a' && lower <= 'f') {
                    digit = static_cast<uint32_t>(lower - 'a') + 10;
                }
                if (digit >= base) {
                    return std::nullopt;
                }
                value = value * base + digit;
                if (value > kMaxCodePoint) {
                    return std::nullopt;
                }
            }
            return value;
        }

        /// Decodes the character reference at the start of @p text into @p out; returns the characters it took, 0 for none
        size_t DecodeMarkupReference(StringView text, String& out) {
            for (const NamedEntity& entity : kXmlEntities) {
                if (text.starts_with(entity.name)) {
                    out.push_back(entity.character);
                    return entity.name.size();
                }
            }
            const size_t end = text.find(';');
            if (!text.starts_with(kNumericReference) || end == StringView::npos) {
                return 0;
            }
            // `&#X..;` is not XML, but a HTML file may say it
            const bool hex = end >= kHexReference.size() && ToLowerAscii(text[kHexReference.size() - 1]) == kHexReference.back();
            const size_t start = hex ? kHexReference.size() : kNumericReference.size();
            const Optional<uint32_t> codePoint = ParseCodePoint(text.substr(start, end - start), hex ? 16 : 10);
            if (!codePoint) {
                return 0;
            }
            AppendCharacter(out, *codePoint);
            return end + 1;
        }

        /// The code unit of a `\uXXXX` at the start of @p text
        Optional<uint32_t> JsonCodeUnit(StringView text) {
            if (!text.starts_with(kJsonUnicodeEscape) || text.size() < kJsonUnicodeLength) {
                return std::nullopt;
            }
            return ParseCodePoint(text.substr(kJsonUnicodeEscape.size(), kJsonUnicodeDigits), 16);
        }

        /// Decodes the escape at the start of @p text into @p out; returns the characters it took, 0 for none
        size_t DecodeJsonEscape(StringView text, String& out) {
            if (text.size() < 2 || text[0] != '\\') {
                return 0;
            }
            if (const Optional<uint32_t> unit = JsonCodeUnit(text)) {
                size_t taken = kJsonUnicodeLength;
                uint32_t codePoint = *unit;
                if (codePoint >= kHighSurrogateFirst && codePoint < kLowSurrogateFirst) {
                    const Optional<uint32_t> low = JsonCodeUnit(text.substr(taken));
                    if (low && *low >= kLowSurrogateFirst && *low < kSurrogateEnd) {
                        codePoint = kSupplementaryFirst + ((codePoint - kHighSurrogateFirst) << kSurrogateBits) + (*low - kLowSurrogateFirst);
                        taken += kJsonUnicodeLength;
                    }
                }
                AppendCharacter(out, codePoint);
                return taken;
            }
            const char escaped = text[1];
            out.push_back(escaped == '/' || escaped == '\\' || escaped == '"' ? escaped : ' ');
            return 2;
        }

        /// @p text without its comments, its references and escapes replaced by what they stand for and `\` as `/`. The lines stay
        String ReadableText(String text, SourceSyntax syntax) {
            if (syntax == SourceSyntax::Markup) {
                BlankMarkupComments(text);
            } else if (syntax == SourceSyntax::Style) {
                BlankStyleComments(text, 0, text.size());
            }

            String decoded;
            decoded.reserve(text.size());
            for (size_t i = 0; i < text.size();) {
                size_t taken = 0;
                if (syntax == SourceSyntax::Markup && text[i] == '&') {
                    taken = DecodeMarkupReference(StringView(text).substr(i), decoded);
                } else if (syntax == SourceSyntax::Json && text[i] == '\\') {
                    taken = DecodeJsonEscape(StringView(text).substr(i), decoded);
                }
                if (taken == 0) {
                    decoded.push_back(text[i]);
                    taken = 1;
                }
                i += taken;
            }

            eastl::replace(decoded.begin(), decoded.end(), '\\', '/');
            return decoded;
        }

        /// Whether @p link, written in a file of @p folder, leads to @p path; a leading `/` starts at the virtual root, where an empty @p folder is
        bool LeadsTo(StringView link, StringView folder, StringView path) {
            while (!link.empty() && IsSpace(link.front())) {
                link.remove_prefix(1);
            }
            while (!link.empty() && IsSpace(link.back())) {
                link.remove_suffix(1);
            }
            const auto leads = [folder, path](StringView text) {
                if (text.empty()) {
                    return false;
                }
                const Path joined = text.front() == '/' ? Path(text.substr(1)) : Path(folder) / text;
                return joined.LexicallyNormal().View() == path;
            };
            if (leads(link)) {
                return true;
            }
            const size_t suffix = link.find_first_of("?#");
            return suffix != StringView::npos && leads(link.substr(0, suffix));
        }

        /// Whether a word, a quoted text or a bracketed text of @p line leads to @p path from the virtual root or from @p folder
        bool HasLinkTo(StringView line, StringView folder, StringView path) {
            const auto matches = [folder, path](StringView link) { return LeadsTo(link, {}, path) || (!folder.empty() && LeadsTo(link, folder, path)); };
            for (size_t start = line.find_first_not_of(kWordDelimiters); start != StringView::npos;) {
                const size_t end = line.find_first_of(kWordDelimiters, start);
                if (matches(line.substr(start, end == StringView::npos ? StringView::npos : end - start))) {
                    return true;
                }
                start = end == StringView::npos ? end : line.find_first_not_of(kWordDelimiters, end);
            }
            // A name with a space is whole only between its quotes or brackets. Each opening one is tried: an apostrophe of a text pairs with nothing
            for (size_t i = 0; i < line.size(); ++i) {
                const char c = line[i];
                if (c != '"' && c != '\'' && c != '(') {
                    continue;
                }
                const size_t close = line.find(c == '(' ? ')' : c, i + 1);
                if (close != StringView::npos && matches(line.substr(i + 1, close - i - 1))) {
                    return true;
                }
            }
            return false;
        }

        bool NamesTarget(StringView line, StringView folder, const Target& target) {
            // Every form of the path ends in the file's name
            if (line.find(target.fileName) == StringView::npos) {
                return false;
            }
            for (size_t at = line.find(target.path); at != StringView::npos; at = line.find(target.path, at + 1)) {
                if (IsDelimited(line, at, at + target.path.size())) {
                    return true;
                }
            }
            return HasLinkTo(line, folder, target.path);
        }

        /// Whether the text at @p at is the whole value of a `ref` attribute
        bool IsRefValue(StringView text, size_t at, size_t length) {
            if (at == 0 || at + length >= text.size() || (text[at - 1] != '"' && text[at - 1] != '\'') || text[at + length] != text[at - 1]) {
                return false;
            }
            size_t before = at - 1;
            const auto skipSpaces = [&before, text] {
                while (before > 0 && IsSpace(text[before - 1])) {
                    --before;
                }
            };
            skipSpaces();
            if (before == 0 || text[before - 1] != '=') {
                return false;
            }
            --before;
            skipSpaces();
            constexpr StringView kName = kNodeRefAttribute;
            return before > kName.size() && text.substr(before - kName.size(), kName.size()) == kName && IsSpace(text[before - kName.size() - 1]);
        }

        /// Appends to @p lines the numbers of the lines of @p text, a SearchableText, that name the target
        void CollectLines(StringView text, StringView folder, const Target& target, Vector<uint32_t>& lines) {
            uint32_t number = 1;
            for (size_t start = 0; start < text.size(); ++number) {
                const size_t end = text.find('\n', start);
                const StringView line = text.substr(start, end == StringView::npos ? StringView::npos : end - start);
                if (NamesTarget(line, folder, target)) {
                    lines.push_back(number);
                }
                start = end == StringView::npos ? text.size() : end + 1;
            }

            if (target.nodeRef.empty()) {
                return;
            }
            // A reference with a folder is told from a word, so any attribute or element may hold it; a bare one only as `ref`
            const bool hasFolder = target.nodeRef.find('/') != String::npos;
            for (size_t at = text.find(target.nodeRef); at != StringView::npos; at = text.find(target.nodeRef, at + 1)) {
                if ((hasFolder && IsDelimited(text, at, at + target.nodeRef.size())) || IsRefValue(text, at, target.nodeRef.size())) {
                    lines.push_back(static_cast<uint32_t>(eastl::count(text.begin(), text.begin() + at, '\n')) + 1);
                }
            }
        }

        const SourceExtension* FindSource(StringView name) {
            const String extension = ToLowerAscii(PathExtension(name));
            for (const SourceExtension& source : kSourceExtensions) {
                if (source.extension == extension) {
                    return &source;
                }
            }
            return nullptr;
        }

        /**
         * Walks the tree of @p folder, gives each file's path below it to @p onFile, and records as `<name>/...` every folder the walk cannot
         * see into: one that cannot be listed, and a link, which no listing of content enters although a path through it loads.
         */
        void WalkSources(const Path& folder, StringView name, Vector<String>& unchecked, FunctionRef<void(StringView relativePath)> onFile) {
            const auto listed = WalkDirectory(folder.View(), WalkMode::Tree, [&](StringView relative, FileKind kind) {
                if (kind == FileKind::File) {
                    onFile(relative);
                } else if (kind == FileKind::Directory) {
                    const Path inner = folder / relative;
                    if (IsSymbolicLink(inner.View()) || !WalkDirectory(inner.View(), WalkMode::Folder, [](StringView, FileKind) { return false; })) {
                        unchecked.push_back(Format("{}/{}/", name, relative));
                    }
                }
                return true;
            });
            if (!listed) {
                unchecked.push_back(Format("{}/", name));
            }
        }

        /// Takes a source file that was read: its name (virtual, or below the content root), its row of `kSourceExtensions` and its text
        using SourceVisitor = FunctionRef<void(StringView name, const SourceExtension& source, String text)>;

        void ReadSource(const Path& realPath, StringView name, const SourceExtension& source, Vector<String>& unchecked, SourceVisitor onSource) {
            auto text = ReadTextFile(realPath);
            // A zero byte is a UTF-16 file, which the engine's XML reader takes and this search cannot
            if (!text || text->find('\0') != String::npos) {
                unchecked.push_back(String(name));
                return;
            }
            onSource(name, source, std::move(*text));
        }

        /// Walks every layer of the mount points: records what cannot be seen into and gives each file to @p onFile, once per layer that has it
        void WalkMounts(const FileSystem& fileSystem, Vector<String>& unchecked, FunctionRef<void(StringView mount, StringView relativePath)> onFile) {
            for (const PoolString mount : GetAssetMounts()) {
                const StringView name = mount.ToStringView();
                for (const Path& layer : fileSystem.GetMountLayers(mount)) {
                    WalkSources(layer, name, unchecked, [&onFile, name](StringView relative) { onFile(name, relative); });
                }
            }
        }

        /// The source files of the mount points but @p skipped, each as the top layer has it
        void ReadMountSources(const FileSystem& fileSystem, StringView skipped, Vector<String>& unchecked, SourceVisitor onSource) {
            Array<StringView, eastl::size(kSourceExtensions)> extensions;
            for (size_t i = 0; i < extensions.size(); ++i) {
                extensions[i] = kSourceExtensions[i].extension;
            }
            for (const PoolString mount : GetAssetMounts()) {
                for (const PoolString file : fileSystem.EnumerateFiles(mount, Span<const StringView>(extensions.data(), extensions.size()))) {
                    const StringView name = file.ToStringView();
                    const SourceExtension* source = FindSource(name);
                    if (source != nullptr && name != skipped && !IsAssetSidecar(name)) {
                        ReadSource(fileSystem.ResolvePath(name), name, *source, unchecked, onSource);
                    }
                }
            }
        }

        /// The JSON files of the Generate panel, which name pictures by paths from the content root
        void ReadArtGenSources(const FileSystem& fileSystem, Vector<String>& unchecked, SourceVisitor onSource) {
            const Path folder = fileSystem.GetRootPath() / kArtGenFolder;
            const Optional<FileInfo> info = GetFileInfo(folder.View());
            if (!info || info->kind != FileKind::Directory) {
                return;
            }
            Vector<String> files;
            WalkSources(folder, kArtGenFolder, unchecked, [&files](StringView relative) {
                const SourceExtension* source = FindSource(relative);
                if (source != nullptr && source->syntax == SourceSyntax::Json && !IsAssetSidecar(relative)) {
                    files.push_back(String(relative));
                }
            });
            eastl::sort(files.begin(), files.end(), [](const String& left, const String& right) { return PathCompare(left, right) < 0; });
            for (const String& relative : files) {
                ReadSource(folder / relative, Format("{}/{}", kArtGenFolder, relative), *FindSource(relative), unchecked, onSource);
            }
        }

        /// A source file as a use of an asset is looked for in it
        struct SearchedSource {
            String name;
            /// Where a relative link of the file starts; empty when it has none
            String folder;
            String text;  ///< readable, in lower case
        };

        SearchedSource MakeSearchedSource(StringView name, const SourceExtension& source, StringView readable) {
            return {String(name), source.linksFromFolder ? ToLowerAscii(PathParent(name)) : String(), ToLowerAscii(readable)};
        }

        Target MakeTarget(StringView assetPath) {
            Target target;
            target.path = ToLowerAscii(assetPath);
            target.fileName = String(PathFilename(target.path));
            const StringView mount = kNodeFilesMountPoint;
            if (PathIsUnder(assetPath, mount) && assetPath.size() > mount.size() + 1 + kNodeFileExtension.size() && StringView(target.path).ends_with(kNodeFileExtension)) {
                const StringView ref = assetPath.substr(mount.size() + 1, assetPath.size() - mount.size() - 1 - kNodeFileExtension.size());
                if (IsValidNodeRef(ref)) {
                    target.nodeRef = ToLowerAscii(ref);
                }
            }
            return target;
        }

        void SortUnique(Vector<String>& texts) {
            eastl::sort(texts.begin(), texts.end());
            texts.erase(eastl::unique(texts.begin(), texts.end()), texts.end());
        }

        /// Whether a name of the content leads to a file
        class ContentFiles {
        public:
            /// @p files: the virtual paths of the files of every layer
            ContentFiles(const FileSystem& fileSystem, const Vector<String>& files) : _fileSystem(fileSystem) {
                _lowered.reserve(files.size());
                for (const String& file : files) {
                    _lowered.push_back(ToLowerAscii(StringView(file)));
                }
                eastl::sort(_lowered.begin(), _lowered.end());
            }

            /// Whether a layer has the file of @p path, ASCII case ignored, or for an XML file of `config` its cooked form
            [[nodiscard]] bool Has(StringView path) const {
                if (HasFile(path)) {
                    return true;
                }
                const StringView extension = PathExtension(path);
                return PathIsUnder(ToLowerAscii(path), MountPoints::config.ToPoolString().ToStringView()) && EqualsIgnoreAsciiCase(extension, kNodeFileExtension) &&
                       HasFile(Format("{}{}", path.substr(0, path.size() - extension.size()), kNodeBinaryExtension));
            }

        private:
            [[nodiscard]] bool HasFile(StringView path) const {
                // The listing enters no linked folder, a path through one loads all the same
                return eastl::binary_search(_lowered.begin(), _lowered.end(), ToLowerAscii(path)) || _fileSystem.Exists(path);
            }

            const FileSystem& _fileSystem;
            Vector<String> _lowered;
        };

        /// The listed file name that @p text is, without the end of a sentence or the suffix of a link; empty when it is none
        StringView ListedName(StringView text) {
            while (!text.empty() && kSentenceEnd.find(text.back()) != StringView::npos) {
                text.remove_suffix(1);
            }
            if (AssetKindOfName(text) == AssetKind::None) {
                text = text.substr(0, text.find_first_of(kLinkSuffixStart));
            }
            const bool isName = AssetKindOfName(text) != AssetKind::None && text.find_first_of(kPatternCharacters) == StringView::npos;
            return isName ? text : StringView();
        }

        /// Whether a path from the virtual root may start at @p at: it is no piece of a longer path or of a URL
        bool StartsPath(StringView line, size_t at) {
            if (at == 0) {
                return true;
            }
            const char before = line[at - 1];
            return !IsNameCharacter(before) && before != '.' && before != '/' && before != ':';
        }

        /// The whole quoted, bracketed or element text that starts at @p at; empty when none does
        StringView ValueAt(StringView line, size_t at) {
            size_t before = at;
            while (before > 0 && IsSpace(line[before - 1])) {
                --before;
            }
            if (before == 0) {
                return {};
            }
            const char open = line[before - 1];
            if (open != '"' && open != '\'' && open != '(' && open != '>') {
                return {};
            }
            size_t end = line.find(open == '(' ? ')' : open == '>' ? '<' : open, at);
            if (end == StringView::npos) {
                return {};
            }
            while (end > at && IsSpace(line[end - 1])) {
                --end;
            }
            return line.substr(at, end - at);
        }

        /// Whether the path @p name leads nowhere; a path whose `..` steps leave the mount points is a relative one and not judged
        bool IsMissing(const ContentFiles& files, StringView name) {
            const Path normal = Path(name).LexicallyNormal();
            const String lower = ToLowerAscii(normal.View());
            const bool mounted = eastl::any_of(GetAssetMounts().begin(), GetAssetMounts().end(), [&lower](PoolString mount) { return PathIsUnder(lower, mount.ToStringView()); });
            return mounted && !files.Has(normal.View());
        }

        /// The missing name of a path that starts at @p at of @p line; empty when there is none
        StringView MissingPathAt(const ContentFiles& files, StringView line, size_t at) {
            const size_t end = line.find_first_of(kWordDelimiters, at);
            const StringView word = line.substr(at, end == StringView::npos ? StringView::npos : end - at);

            // A name with a space is whole only as a value of its own
            StringView spaced;
            const StringView value = ValueAt(line, at);
            if (value.size() > word.size() && value[word.size()] == ' ' && !word.ends_with('/') && value.find_first_of(kNotInAValue) == StringView::npos) {
                spaced = ListedName(value);
                if (!spaced.empty() && !IsMissing(files, spaced)) {
                    return {};
                }
            }
            const StringView name = ListedName(word);
            const StringView found = name.empty() ? spaced : name;
            return !found.empty() && IsMissing(files, found) ? found : StringView();
        }

        /// Appends the paths of @p readable, the ReadableText of the file @p file, that lead nowhere
        void CollectMissingPaths(const ContentFiles& files, StringView file, StringView readable, Vector<MissingAsset>& out) {
            uint32_t number = 1;
            for (size_t start = 0; start < readable.size(); ++number) {
                const size_t end = readable.find('\n', start);
                const StringView line = readable.substr(start, end == StringView::npos ? StringView::npos : end - start);
                const String lower = ToLowerAscii(line);
                for (const PoolString mount : GetAssetMounts()) {
                    const StringView folder = mount.ToStringView();
                    for (size_t at = StringView(lower).find(folder); at != StringView::npos; at = StringView(lower).find(folder, at + 1)) {
                        if (at + folder.size() >= line.size() || line[at + folder.size()] != '/' || !StartsPath(line, at)) {
                            continue;
                        }
                        if (const StringView name = MissingPathAt(files, line, at); !name.empty()) {
                            out.push_back({String(name), {String(file), number}});
                        }
                    }
                }
                start = end == StringView::npos ? readable.size() : end + 1;
            }
        }

        /// Appends the references of the `<node>` elements at and below @p element that lead to no node file
        void CollectMissingRefs(const ContentFiles& files, StringView file, StringView text, pugi::xml_node element, Vector<MissingAsset>& out) {
            if (const pugi::xml_attribute ref = element.attribute(kNodeRefAttribute.data()); ref && StringView(element.name()) == kNodeElementName) {
                const StringView value = ref.value();
                if (!IsValidNodeRef(value) || !files.Has(Format("{}/{}{}", kNodeFilesMountPoint, value, kNodeFileExtension))) {
                    const auto line = static_cast<uint32_t>(eastl::count(text.begin(), text.begin() + element.offset_debug(), '\n')) + 1;
                    out.push_back({String(value), {String(file), line}});
                }
            }
            for (const pugi::xml_node child : element.children()) {
                CollectMissingRefs(files, file, text, child, out);
            }
        }

        /// Checks the node references of the XML file @p file of `config` when it is a scene or a node file, and notes a node file
        void CheckSceneOrNodeFile(const ContentFiles& files, StringView file, StringView text, Vector<String>& nodeFiles, ContentAssetCheck& out) {
            // The offsets of the parser then index the same text
            if (text.starts_with(kUtf8ByteOrderMark)) {
                text.remove_prefix(kUtf8ByteOrderMark.size());
            }
            pugi::xml_document document;
            const bool parsed = static_cast<bool>(document.load_buffer(text.data(), text.size(), pugi::parse_default, pugi::encoding_utf8));
            const pugi::xml_node root = document.document_element();
            const AssetKind kind = AssetKindOfConfigXml(file, parsed ? StringView(root.name()) : StringView());
            if (kind != AssetKind::Scene && kind != AssetKind::Node) {
                return;
            }
            if (!parsed) {
                out.unchecked.push_back(String(file));
                return;
            }
            if (kind == AssetKind::Node) {
                nodeFiles.push_back(String(file));
            }
            CollectMissingRefs(files, file, text, root, out.missing);
        }

        /// The order of `ContentAssetCheck::missing`
        bool IsListedBefore(const MissingAsset& left, const MissingAsset& right) {
            if (left.use.file != right.use.file) {
                return left.use.file < right.use.file;
            }
            return left.use.line != right.use.line ? left.use.line < right.use.line : left.name < right.name;
        }

        bool IsUsed(StringView assetPath, const Vector<SearchedSource>& sources) {
            const Target target = MakeTarget(assetPath);
            Vector<uint32_t> lines;
            for (const SearchedSource& source : sources) {
                // Every use holds the file's name or the node reference, so most files are done with here
                const bool mayName = source.text.find(target.fileName) != String::npos || (!target.nodeRef.empty() && source.text.find(target.nodeRef) != String::npos);
                if (mayName && source.name != assetPath) {
                    CollectLines(source.text, source.folder, target, lines);
                    if (!lines.empty()) {
                        return true;
                    }
                }
            }
            return false;
        }

    }  // namespace

    AssetReferences FindAssetReferences(const FileSystem& fileSystem, StringView assetPath) {
        AssetReferences references;
        const Target target = MakeTarget(assetPath);
        if (target.fileName.empty()) {
            return references;
        }
        const auto search = [&references, &target](StringView name, const SourceExtension& source, String text) {
            const SearchedSource searched = MakeSearchedSource(name, source, ReadableText(std::move(text), source.syntax));
            Vector<uint32_t> lines;
            CollectLines(searched.text, searched.folder, target, lines);
            eastl::sort(lines.begin(), lines.end());
            lines.erase(eastl::unique(lines.begin(), lines.end()), lines.end());
            for (const uint32_t line : lines) {
                references.uses.push_back({searched.name, line});
            }
        };
        WalkMounts(fileSystem, references.unchecked, [](StringView, StringView) {});
        ReadMountSources(fileSystem, assetPath, references.unchecked, search);
        ReadArtGenSources(fileSystem, references.unchecked, search);

        // A folder of a mount point is walked once per layer
        SortUnique(references.unchecked);
        return references;
    }

    ContentAssetCheck CheckContentAssets(const FileSystem& fileSystem) {
        ContentAssetCheck check;
        Vector<String> files;
        WalkMounts(fileSystem, check.unchecked, [&files](StringView mount, StringView relative) {
            String path = Format("{}/{}", mount, relative);
            PathToGenericSeparators(path);
            files.push_back(std::move(path));
        });
        SortUnique(files);
        const ContentFiles content(fileSystem, files);

        Vector<SearchedSource> sources;
        Vector<String> nodeFiles;
        ReadMountSources(fileSystem, {}, check.unchecked, [&](StringView name, const SourceExtension& source, String text) {
            if (AssetKindOfName(name) == AssetKind::Config) {
                CheckSceneOrNodeFile(content, name, text, nodeFiles, check);
            }
            String readable = ReadableText(std::move(text), source.syntax);
            sources.push_back(MakeSearchedSource(name, source, readable));
            if (source.syntax == SourceSyntax::Markup && source.linksFromFolder) {
                BlankEmbeddedStyleComments(readable);
            }
            CollectMissingPaths(content, name, readable, check.missing);
        });
        ReadArtGenSources(fileSystem, check.unchecked, [&sources](StringView name, const SourceExtension& source, String text) {
            sources.push_back(MakeSearchedSource(name, source, ReadableText(std::move(text), source.syntax)));
        });

        for (const String& file : files) {
            AssetKind kind = AssetKindOfName(file);
            if (kind == AssetKind::Config && eastl::find(nodeFiles.begin(), nodeFiles.end(), file) != nodeFiles.end()) {
                kind = AssetKind::Node;
            }
            if (IsChangeableAssetKind(kind) && ResolveContentRootFile(fileSystem, file) && !IsUsed(file, sources)) {
                check.unused.push_back(file);
            }
        }

        eastl::sort(check.missing.begin(), check.missing.end(), IsListedBefore);
        // One line may hold a name twice
        const auto isSame = [](const MissingAsset& left, const MissingAsset& right) { return !IsListedBefore(left, right) && !IsListedBefore(right, left); };
        check.missing.erase(eastl::unique(check.missing.begin(), check.missing.end(), isSame), check.missing.end());
        SortUnique(check.unchecked);
        return check;
    }

}  // namespace BECore
