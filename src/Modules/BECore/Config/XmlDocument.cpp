#include "XmlDocument.h"

#include <BECore/Config/StringXmlWriter.h>
#include <BECore/Config/XmlLayoutMerge.h>
#include <BECore/File/File.h>
#include <BECore/File/FilePath.h>
#include <BECore/Reflection/ReaderLimits.h>

namespace BECore {

    namespace {

        /// Comments and the declaration stay in the document, so a save writes them back
        constexpr unsigned kParseFlags = pugi::parse_default | pugi::parse_comments | pugi::parse_declaration;

        /// No `format_indent_attributes` (one line per element) and no declaration the file did not have
        constexpr unsigned kWriteFlags = pugi::format_indent | pugi::format_no_declaration;

        constexpr const char* kIndent = "  ";

        constexpr StringView kByteOrderMark = "\xEF\xBB\xBF";

        bool StartsWithByteOrderMark(StringView text) {
            return text.size() >= kByteOrderMark.size() && text.substr(0, kByteOrderMark.size()) == kByteOrderMark;
        }

        StringView WithoutByteOrderMark(StringView text) {
            return StartsWithByteOrderMark(text) ? text.substr(kByteOrderMark.size()) : text;
        }

        /// The canonical text of @p document, the one every save without a layout to keep writes
        String SerializeCanonical(const pugi::xml_document& document, bool withByteOrderMark) {
            StringXmlWriter writer;
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
            const String baseline = SerializeCanonical(diskDocument, false);
            Optional<String> merged = MergeXmlLayout(onDisk, baseline, updated);
            if (!merged) {
                return std::nullopt;
            }

            pugi::xml_document mergedDocument;
            if (!mergedDocument.load_buffer(merged->data(), merged->size(), kParseFlags, pugi::encoding_utf8) || SerializeCanonical(mergedDocument, false) != updated) {
                LOG_DEBUG("XmlDocument: the merged layout does not give the document, the canonical text is written");
                return std::nullopt;
            }
            return merged;
        }

    }  // namespace

    Optional<String> XmlDocument::ReadFileBytes(const std::filesystem::path& path) {
        auto text = ReadTextFile(PathToUtf8(path), ReaderLimits::kMaxXmlDocumentBytes);
        if (!text) {
            return std::nullopt;
        }
        return eastl::move(*text);
    }

    Optional<StringView> XmlDocument::ReadFileInto(const std::filesystem::path& path, LinearArena& arena, size_t maxBytes) {
        auto file = File::Open(PathToUtf8(path), FileMode::Read);
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

    bool XmlDocument::LoadFromFile(const std::filesystem::path& filepath) {
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

    bool XmlDocument::SaveToFile(const std::filesystem::path& filepath) const {
        const auto parentPath = filepath.parent_path();
        if (!parentPath.empty() && !CreateDirectories(PathToUtf8(parentPath))) {
            return false;
        }

        // The text is built before the file is opened, which truncates it
        String text = Serialize();

        // The DOM wins over the file: what is on disk only lends its layout (blank lines, indentation)
        if (const Optional<String> onDisk = ReadFileBytes(filepath)) {
            const StringView updatedBody = WithoutByteOrderMark(text);
            const size_t markSize = text.size() - updatedBody.size();
            if (const Optional<String> laidOut = LayOutLikeFile(WithoutByteOrderMark(*onDisk), updatedBody)) {
                text.resize(markSize);
                text.append(laidOut->data(), laidOut->size());
            }
            if (*onDisk == text) {
                return true;
            }
        }

        // In place: a watcher of the folder sees one change of the file, not a temp file and a rename. Every File mode is binary
        auto out = File::Open(PathToUtf8(filepath), FileMode::Write);
        return out && out->Write(StringView(text)) && out->Close().has_value();
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
