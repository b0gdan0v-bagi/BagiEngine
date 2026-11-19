#include "XmlDocument.h"

#include <BECore/Config/XmlLayoutMerge.h>
#include <BECore/Reflection/ReaderLimits.h>

namespace BECore {

    namespace {

        /// Comments and the declaration stay in the document, so a save writes them back
        constexpr unsigned kParseFlags = pugi::parse_default | pugi::parse_comments | pugi::parse_declaration;

        /// No `format_indent_attributes` (one line per element) and no declaration the file did not have
        constexpr unsigned kWriteFlags = pugi::format_indent | pugi::format_no_declaration;

        constexpr const char* kIndent = "  ";

        constexpr StringView kByteOrderMark = "\xEF\xBB\xBF";

        class StringWriter final : public pugi::xml_writer {
        public:
            explicit StringWriter(String& out) : _out(out) {}

            void write(const void* data, size_t size) override {
                _out.append(static_cast<const char*>(data), size);
            }

        private:
            String& _out;
        };

        bool StartsWithByteOrderMark(StringView text) {
            return text.size() >= kByteOrderMark.size() && text.substr(0, kByteOrderMark.size()) == kByteOrderMark;
        }

        StringView WithoutByteOrderMark(StringView text) {
            return StartsWithByteOrderMark(text) ? text.substr(kByteOrderMark.size()) : text;
        }

        /// The canonical text of @p document, the one every save without a layout to keep writes
        String SerializeCanonical(const pugi::xml_document& document, bool withByteOrderMark) {
            String text;
            StringWriter writer(text);
            document.save(writer, kIndent, kWriteFlags | (withByteOrderMark ? pugi::format_write_bom : 0u), pugi::encoding_utf8);
            return text;
        }

        /// The bytes of the file at @p path, or nothing when it is not a regular file, is bigger than the limit of an XML
        /// document or cannot be read: the layout of such a file is not worth keeping
        Optional<String> ReadExistingFile(const std::filesystem::path& path) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                return std::nullopt;
            }
            const uintmax_t size = std::filesystem::file_size(path, error);
            if (error || size > ReaderLimits::kMaxXmlDocumentBytes) {
                return std::nullopt;
            }
            std::ifstream in(path, std::ios::in | std::ios::binary);  //-NoStd: the document's own file, read byte for byte before it is replaced
            if (!in.is_open()) {
                return std::nullopt;
            }
            String bytes;
            bytes.resize(static_cast<size_t>(size));
            in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (in.gcount() != static_cast<std::streamsize>(bytes.size())) {
                return std::nullopt;
            }
            return bytes;
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

    bool XmlDocument::LoadFromFile(const std::filesystem::path& filepath) {
        if (!std::filesystem::exists(filepath)) {
            return false;
        }

        if (!std::filesystem::is_regular_file(filepath)) {
            return false;
        }

        std::ifstream testStream(filepath, std::ios::in | std::ios::binary);
        if (!testStream.is_open()) {
            return false;
        }
        char head[3] = {};
        testStream.read(head, sizeof(head));
        const bool hasByteOrderMark = testStream.gcount() == sizeof(head) && StartsWithByteOrderMark(StringView(head, sizeof(head)));
        testStream.close();

        pugi::xml_parse_result result = _doc.load_file(filepath.string().c_str(), kParseFlags);
        _hasByteOrderMark = result && hasByteOrderMark;
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
        auto parentPath = filepath.parent_path();
        if (!parentPath.empty()) {
            if (!std::filesystem::exists(parentPath)) {
                std::error_code ec;
                if (!std::filesystem::create_directories(parentPath, ec)) {
                    return false;
                }
            }
        }

        // The text is built before the file is opened, which truncates it
        String text = Serialize();

        // The DOM wins over the file: what is on disk only lends its layout (blank lines, indentation)
        if (const Optional<String> onDisk = ReadExistingFile(filepath)) {
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

        // Binary: a text-mode stream on Windows would turn every '\n' into "\r\n"
        std::ofstream out(filepath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        return !out.fail();
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
