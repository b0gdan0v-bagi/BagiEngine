#include "XmlDocument.h"

#include <BECore/Config/StringXmlWriter.h>
#include <BECore/Config/XmlLayoutMerge.h>
#include <BECore/File/File.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/Reflection/ReaderLimits.h>

namespace BECore {

    namespace {

        /// Comments and the declaration stay in the document, so a save writes them back
        constexpr unsigned kParseFlags = pugi::parse_default | pugi::parse_comments | pugi::parse_declaration;

        /// No `format_indent_attributes` (one line per element) and no declaration the file did not have
        constexpr unsigned kWriteFlags = pugi::format_indent | pugi::format_no_declaration;

        constexpr const char* kIndent = "  ";

        /// An on-disk config bigger than this is read into a heap block: the chunks of a scratch arena stay allocated after the scope, and a
        /// thread must not keep one the size of the biggest file it ever saved over (ConfigManager's bound for its reads)
        constexpr size_t kMaxScratchSaveBytes = 256 * 1024;

        constexpr StringView kByteOrderMark = "\xEF\xBB\xBF";

        bool StartsWithByteOrderMark(StringView text) {
            return text.size() >= kByteOrderMark.size() && text.substr(0, kByteOrderMark.size()) == kByteOrderMark;
        }

        StringView WithoutByteOrderMark(StringView text) {
            return StartsWithByteOrderMark(text) ? text.substr(kByteOrderMark.size()) : text;
        }

        /// The canonical text of @p document, the one every save without a layout to keep writes
        String SerializeCanonical(const pugi::xml_document& document, bool withByteOrderMark, size_t sizeHint = 0) {
            StringXmlWriter writer(sizeHint);
            document.save(writer, kIndent, kWriteFlags | (withByteOrderMark ? pugi::format_write_bom : 0u), pugi::encoding_utf8);
            return eastl::move(writer.text);
        }

        /// @p updated laid out like @p onDisk, or nothing when that is not possible or does not give the same document
        ///
        /// The merge only moves lines, so the check is made on the document: the laid-out text must parse to what @p updated
        /// serializes to, and a mistake of the merge costs the layout, never the content.
        Optional<String> LayOutLikeFile(StringView onDisk, StringView updated) {
            pugi::xml_document diskDocument;
            if (!diskDocument.load_buffer(onDisk.data(), onDisk.size(), kParseFlags, pugi::encoding_utf8)) {
                return std::nullopt;
            }
            // The canonical text of a hand-laid file is about its size: one reserve instead of growth by doubling
            const String baseline = SerializeCanonical(diskDocument, false, onDisk.size());
            Optional<String> merged = MergeXmlLayout(onDisk, baseline, updated);
            if (!merged) {
                return std::nullopt;
            }

            pugi::xml_document mergedDocument;
            if (!mergedDocument.load_buffer(merged->data(), merged->size(), kParseFlags, pugi::encoding_utf8) || SerializeCanonical(mergedDocument, false, updated.size()) != updated) {
                LOG_DEBUG("XmlDocument: the merged layout does not give the document, the canonical text is written");
                return std::nullopt;
            }
            return merged;
        }

    }  // namespace

    Optional<String> XmlDocument::ReadFileBytes(const Path& path) {
        auto text = ReadTextFile(path, ReaderLimits::kMaxXmlDocumentBytes);
        if (!text) {
            return std::nullopt;
        }
        return eastl::move(*text);
    }

    Optional<StringView> XmlDocument::ReadFileInto(const Path& path, LinearArena& arena, size_t maxBytes) {
        auto file = File::Open(path, FileMode::Read);
        if (!file) {
            return std::nullopt;
        }
        const auto size = file->Size();
        // The size of a file is untrusted: it is checked before the arena is asked for it
        if (!size || *size > std::min<uint64_t>(maxBytes, ReaderLimits::kMaxXmlDocumentBytes)) {
            return std::nullopt;
        }
        char* bytes = arena.Allocate<char>(static_cast<size_t>(*size));
        const auto read = file->Read(bytes, static_cast<size_t>(*size));
        if (!read || *read != *size) {
            return std::nullopt;
        }
        return StringView{bytes, static_cast<size_t>(*size)};
    }

    bool XmlDocument::LoadFromFile(const Path& filepath) {
        // One read: the file that cannot be opened (missing, a folder) or is over the limit does not load, and the byte-order
        // mark comes from the same bytes the parser sees
        const Optional<String> bytes = ReadFileBytes(filepath);
        if (!bytes) {
            return false;
        }
        const pugi::xml_parse_result result = _doc.load_buffer(bytes->data(), bytes->size(), kParseFlags, pugi::encoding_auto);
        _hasByteOrderMark = result && StartsWithByteOrderMark(*bytes);
        return result;
    }

    bool XmlDocument::LoadFromString(StringView xmlContent) {
        if (xmlContent.empty()) {
            return false;
        }

        // A StringView is not NUL-terminated: load_string(data()) would read past its end
        pugi::xml_parse_result result = _doc.load_buffer(xmlContent.data(), xmlContent.size(), kParseFlags, pugi::encoding_utf8);
        _hasByteOrderMark = result && StartsWithByteOrderMark(xmlContent);
        return result;
    }

    String XmlDocument::Serialize() const {
        return SerializeCanonical(_doc, _hasByteOrderMark);
    }

    bool XmlDocument::SaveToFile(const Path& filepath) const {
        // The rename that ends an atomic write would put a plain file in the place of a link, so the file the link names is the target
        Path target = filepath;
        if (IsSymbolicLink(filepath)) {
            auto linked = CanonicalPath(filepath);
            if (!linked) {
                return false;
            }
            target = Path(eastl::move(*linked));
        }

        // The text stays on the heap, as it outlives the scratch scope. It is reserved like the file that is there: a hand-laid file is about as
        // long as its canonical text, so a save grows it rarely
        String text;

        // The DOM wins over the file: what is on disk only lends its layout (blank lines, indentation). LayOutLikeFile keeps its two pugixml
        // documents and its canonical texts on the heap (pugixml's allocator, and Strings the comparison with the DOM needs)
        {
            ScratchScope scratch;
            const auto read = ReadFileBytesScratch(target, scratch.GetArena(), kMaxScratchSaveBytes, ReaderLimits::kMaxXmlDocumentBytes);
            const Span<const uint8_t> bytes = read ? read->Bytes() : Span<const uint8_t>{};
            const StringView onDisk(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            text = SerializeCanonical(_doc, _hasByteOrderMark, onDisk.size());
            if (read) {
                const StringView updatedBody = WithoutByteOrderMark(text);
                const size_t markSize = text.size() - updatedBody.size();
                if (const Optional<String> laidOut = LayOutLikeFile(WithoutByteOrderMark(onDisk), updatedBody)) {
                    text.resize(markSize);
                    text.append(laidOut->data(), laidOut->size());
                }
                if (onDisk == StringView(text)) {
                    return true;
                }
            }
        }

        return WriteFileAtomically(target, StringView(text)).has_value();
    }

    XmlNode XmlDocument::GetRoot() const {
        if (!_doc) {
            return {};
        }
        // The first child may be the declaration or a comment
        return XmlNode{_doc.document_element()};
    }

    void XmlDocument::Clear() {
        _doc.reset();
        _hasByteOrderMark = false;
    }

    bool XmlDocument::IsLoaded() const {
        return !_doc.empty();
    }

}  // namespace BECore
