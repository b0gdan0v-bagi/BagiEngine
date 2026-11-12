#include "UIHtmlParser.h"

#include <BECore/UI/UIWebText.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view HtmlSource = "html";
        constexpr eastl::string_view CommentOpen = "<!--";
        constexpr eastl::string_view CommentClose = "-->";
        constexpr int HexBase = 16;
        constexpr int DecimalBase = 10;
        constexpr uint32_t MaxCodePoint = 0x10FFFF;
        constexpr uint32_t FirstSurrogate = 0xD800;  // not characters: UTF-8 has no form for them
        constexpr uint32_t LastSurrogate = 0xDFFF;

        const char* const VoidElements[] = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"};
        const char* const RawTextElements[] = {"style", "script"};

        bool IsOneOf(eastl::string_view tag, const char* const* names, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                if (tag == names[i]) {
                    return true;
                }
            }
            return false;
        }

        bool IsVoidElement(eastl::string_view tag) {
            return IsOneOf(tag, VoidElements, std::size(VoidElements));
        }

        bool IsRawTextElement(eastl::string_view tag) {
            return IsOneOf(tag, RawTextElements, std::size(RawTextElements));
        }

        bool IsNameChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':';
        }

        bool IsAlpha(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        void AppendCodePoint(eastl::string& out, uint32_t codePoint) {
            if (codePoint == 0 || codePoint > MaxCodePoint || (codePoint >= FirstSurrogate && codePoint <= LastSurrogate)) {
                out.push_back('?');
            } else if (codePoint < 0x80) {
                out.push_back(static_cast<char>(codePoint));
            } else if (codePoint < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            } else if (codePoint < 0x10000) {
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

        /// The text with its entities decoded; one that is not known stays as written
        eastl::string DecodeEntities(eastl::string_view text) {
            eastl::string out;
            out.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] != '&') {
                    out.push_back(text[i]);
                    continue;
                }
                const size_t end = text.find(';', i);
                if (end == eastl::string_view::npos || end - i > 10) {
                    out.push_back('&');
                    continue;
                }
                const eastl::string_view entity = text.substr(i + 1, end - i - 1);
                bool known = true;
                if (entity == "amp") {
                    out.push_back('&');
                } else if (entity == "lt") {
                    out.push_back('<');
                } else if (entity == "gt") {
                    out.push_back('>');
                } else if (entity == "quot") {
                    out.push_back('"');
                } else if (entity == "apos") {
                    out.push_back('\'');
                } else if (entity == "nbsp") {
                    AppendCodePoint(out, 0xA0);
                } else if (!entity.empty() && entity[0] == '#') {
                    const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
                    const eastl::string_view digits = entity.substr(hex ? 2 : 1);
                    uint32_t codePoint = 0;
                    const auto [parsedEnd, error] = std::from_chars(digits.data(), digits.data() + digits.size(), codePoint, hex ? HexBase : DecimalBase);
                    known = !digits.empty() && error == std::errc{} && parsedEnd == digits.data() + digits.size();
                    if (known) {
                        AppendCodePoint(out, codePoint);
                    }
                } else {
                    known = false;
                }
                if (known) {
                    i = end;
                } else {
                    out.push_back('&');
                }
            }
            return out;
        }

        class Reader {
        public:
            Reader(eastl::string_view html, UIHtmlDocument& document, UIWebReport& report) : _html(html), _document(document), _report(report) {}

            void Run() {
                UIHtmlNode root;
                _document.nodes.push_back(std::move(root));
                _current = UIHtmlDocument::RootIndex;
                _depths.push_back(0);
                while (_pos < _html.size()) {
                    if (_html[_pos] != '<') {
                        ReadText();
                    } else if (StartsWith(CommentOpen)) {
                        SkipComment();
                    } else if (StartsWith("<!") || StartsWith("<?")) {
                        SkipTo('>');
                    } else if (StartsWith("</")) {
                        ReadEndTag();
                    } else if (_pos + 1 < _html.size() && IsAlpha(_html[_pos + 1])) {
                        ReadStartTag();
                    } else {
                        // A '<' that starts no tag is text
                        AppendText(_html.substr(_pos, 1));
                        Advance(1);
                    }
                }
                if (!_stopped && _current != UIHtmlDocument::RootIndex) {
                    _report.AddOnce(HtmlSource, _document.nodes[_current].line, Format("<{}> is never closed", _document.nodes[_current].tag.c_str()));
                }
            }

        private:
            /// Reports @p why at the current line and ends the read: the rest of the document is left out
            void Stop(eastl::string why) {
                _report.AddOnce(HtmlSource, _line, std::move(why));
                _pos = _html.size();
                _stopped = true;
            }

            [[nodiscard]] bool StartsWith(eastl::string_view prefix) const {
                return _html.substr(_pos, prefix.size()) == prefix;
            }

            void Advance(size_t count) {
                for (size_t i = 0; i < count && _pos < _html.size(); ++i, ++_pos) {
                    if (_html[_pos] == '\n') {
                        ++_line;
                    }
                }
            }

            void SkipTo(char terminator) {
                const size_t end = _html.find(terminator, _pos);
                Advance(end == eastl::string_view::npos ? _html.size() - _pos : end - _pos + 1);
            }

            void SkipComment() {
                const size_t end = _html.find(CommentClose, _pos + CommentOpen.size());
                Advance(end == eastl::string_view::npos ? _html.size() - _pos : end + CommentClose.size() - _pos);
            }

            void SkipSpaces() {
                while (_pos < _html.size() && UIWebText::IsSpace(_html[_pos])) {
                    Advance(1);
                }
            }

            /// The index of the new child of the open element, or NoNode when the document has too many nodes (the read has stopped)
            int AddNode(UIHtmlNode node) {
                if (_document.nodes.size() > UIHtmlParser::kMaxNodes) {
                    Stop(Format("the document has more than {} elements and pieces of text: the rest is left out", UIHtmlParser::kMaxNodes));
                    return UIHtmlNode::NoNode;
                }
                node.parent = _current;
                const int index = static_cast<int>(_document.nodes.size());
                _document.nodes.push_back(std::move(node));
                _depths.push_back(_depths[_current] + 1);
                _document.nodes[_current].children.push_back(index);
                return index;
            }

            void AppendText(eastl::string_view raw, bool decode = true) {
                if (raw.empty()) {
                    return;
                }
                UIHtmlNode text;
                text.text = decode ? DecodeEntities(raw) : eastl::string(raw);
                text.line = _line;
                AddNode(std::move(text));
            }

            void ReadText() {
                const size_t end = _html.find('<', _pos);
                const size_t length = end == eastl::string_view::npos ? _html.size() - _pos : end - _pos;
                const uint32_t startLine = _line;
                const eastl::string_view raw = _html.substr(_pos, length);
                Advance(length);
                const uint32_t endLine = _line;
                _line = startLine;
                AppendText(raw);
                _line = endLine;
            }

            void ReadEndTag() {
                Advance(2);
                const size_t nameStart = _pos;
                while (_pos < _html.size() && IsNameChar(_html[_pos])) {
                    Advance(1);
                }
                const eastl::string name = UIWebText::ToLower(_html.substr(nameStart, _pos - nameStart));
                SkipTo('>');
                // Close the nearest open element of that name
                for (int node = _current; node != UIHtmlDocument::RootIndex; node = _document.nodes[node].parent) {
                    if (_document.nodes[node].tag == name) {
                        _current = _document.nodes[node].parent;
                        return;
                    }
                }
                _report.AddOnce(HtmlSource, _line, Format("</{}> closes nothing", name.c_str()));
            }

            void ReadStartTag() {
                const uint32_t startLine = _line;
                Advance(1);
                const size_t nameStart = _pos;
                while (_pos < _html.size() && IsNameChar(_html[_pos])) {
                    Advance(1);
                }
                UIHtmlNode element;
                element.tag = UIWebText::ToLower(_html.substr(nameStart, _pos - nameStart));
                bool selfClosing = false;
                ReadAttributes(element, selfClosing);

                element.line = startLine;
                if (_depths[_current] >= UIHtmlParser::kMaxDepth) {
                    Stop(Format("elements are nested deeper than {}: the rest of the document is left out", UIHtmlParser::kMaxDepth));
                    return;
                }
                const eastl::string tag = element.tag;
                const int index = AddNode(std::move(element));
                if (index == UIHtmlNode::NoNode || selfClosing || IsVoidElement(tag)) {
                    return;
                }
                if (IsRawTextElement(tag)) {
                    ReadRawText(index, tag);
                    return;
                }
                _current = index;
            }

            void ReadAttributes(UIHtmlNode& element, bool& selfClosing) {
                while (_pos < _html.size()) {
                    SkipSpaces();
                    if (_pos >= _html.size()) {
                        return;
                    }
                    const char c = _html[_pos];
                    if (c == '>') {
                        Advance(1);
                        return;
                    }
                    if (c == '/') {
                        Advance(1);
                        selfClosing = true;
                        continue;
                    }
                    const size_t nameStart = _pos;
                    while (_pos < _html.size() && !UIWebText::IsSpace(_html[_pos]) && _html[_pos] != '=' && _html[_pos] != '>' && _html[_pos] != '/') {
                        Advance(1);
                    }
                    UIHtmlAttribute attribute;
                    attribute.name = UIWebText::ToLower(_html.substr(nameStart, _pos - nameStart));
                    SkipSpaces();
                    if (_pos < _html.size() && _html[_pos] == '=') {
                        Advance(1);
                        SkipSpaces();
                        attribute.value = ReadAttributeValue();
                    }
                    if (!attribute.name.empty()) {
                        if (element.attributes.size() >= UIHtmlParser::kMaxAttributes) {
                            _report.AddOnce(HtmlSource, _line, Format("<{}> has more than {} attributes: the rest are left out", element.tag.c_str(), UIHtmlParser::kMaxAttributes));
                        } else if (attribute.name.size() > UIHtmlParser::kMaxAttributeBytes || attribute.value.size() > UIHtmlParser::kMaxAttributeBytes) {
                            _report.AddOnce(HtmlSource, _line, Format("an attribute of <{}> is longer than {} bytes: it is left out", element.tag.c_str(), UIHtmlParser::kMaxAttributeBytes));
                        } else {
                            element.attributes.push_back(std::move(attribute));
                        }
                    } else {
                        Advance(1);  // a character that is no name: skip it so the loop moves on
                    }
                }
            }

            eastl::string ReadAttributeValue() {
                if (_pos >= _html.size()) {
                    return {};
                }
                const char quote = _html[_pos];
                if (quote == '"' || quote == '\'') {
                    Advance(1);
                    const size_t end = _html.find(quote, _pos);
                    const size_t length = end == eastl::string_view::npos ? _html.size() - _pos : end - _pos;
                    const eastl::string value = DecodeEntities(_html.substr(_pos, length));
                    Advance(length + (end == eastl::string_view::npos ? 0 : 1));
                    return value;
                }
                const size_t start = _pos;
                while (_pos < _html.size() && !UIWebText::IsSpace(_html[_pos]) && _html[_pos] != '>') {
                    Advance(1);
                }
                return DecodeEntities(_html.substr(start, _pos - start));
            }

            /// The content of `<style>` / `<script>` up to its end tag, as one raw text node
            void ReadRawText(int element, const eastl::string& tag) {
                const eastl::string closing = Format("</{}", tag.c_str());
                size_t end = _pos;
                while (true) {
                    end = _html.find('<', end);
                    if (end == eastl::string_view::npos) {
                        break;
                    }
                    if (UIWebText::ToLower(_html.substr(end, closing.size())) == closing) {
                        break;
                    }
                    ++end;
                }
                const size_t length = end == eastl::string_view::npos ? _html.size() - _pos : end - _pos;
                const int saved = _current;
                _current = element;
                const uint32_t startLine = _line;
                const eastl::string_view raw = _html.substr(_pos, length);
                Advance(length);
                const uint32_t endLine = _line;
                _line = startLine;
                AppendText(raw, false);
                _line = endLine;
                _current = saved;
                if (end != eastl::string_view::npos) {
                    SkipTo('>');
                }
            }

            eastl::string_view _html;
            UIHtmlDocument& _document;
            UIWebReport& _report;
            size_t _pos = 0;
            uint32_t _line = 1;
            int _current = UIHtmlDocument::RootIndex;
            bool _stopped = false;            ///< a limit ended the read: what is still open is not "never closed"
            eastl::vector<uint32_t> _depths;  ///< per node, the number of elements open around it (the document node: 0)
        };

    }  // namespace

    const eastl::string* UIHtmlNode::FindAttribute(eastl::string_view name) const {
        for (const UIHtmlAttribute& attribute : attributes) {
            if (attribute.name == name) {
                return &attribute.value;
            }
        }
        return nullptr;
    }

    bool UIHtmlNode::HasClass(eastl::string_view className) const {
        const eastl::string* value = FindAttribute("class");
        if (value == nullptr) {
            return false;
        }
        // No list of the words is made: the compile asks this for every element and every class of every rule
        const eastl::string_view classes(value->data(), value->size());
        size_t pos = 0;
        while (pos < classes.size()) {
            while (pos < classes.size() && UIWebText::IsSpace(classes[pos])) {
                ++pos;
            }
            const size_t start = pos;
            while (pos < classes.size() && !UIWebText::IsSpace(classes[pos])) {
                ++pos;
            }
            if (pos > start && classes.substr(start, pos - start) == className) {
                return true;
            }
        }
        return false;
    }

    UIHtmlDocument UIHtmlParser::Parse(eastl::string_view html, UIWebReport& report) {
        UIHtmlDocument document;
        if (html.size() > kMaxDocumentBytes) {
            report.AddOnce(HtmlSource, 1, Format("the document is longer than {} bytes: the rest is left out", kMaxDocumentBytes));
            html = html.substr(0, kMaxDocumentBytes);
        }
        Reader(html, document, report).Run();
        return document;
    }

}  // namespace BECore
