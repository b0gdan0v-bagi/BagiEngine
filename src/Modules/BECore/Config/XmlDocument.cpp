#include "XmlDocument.h"

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
        String text;
        StringWriter writer(text);
        _doc.save(writer, kIndent, kWriteFlags | (_hasByteOrderMark ? pugi::format_write_bom : 0u), pugi::encoding_utf8);
        return text;
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
        const String text = Serialize();

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
