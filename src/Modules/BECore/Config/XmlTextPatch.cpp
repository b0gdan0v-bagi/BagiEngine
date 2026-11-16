#include "XmlTextPatch.h"

#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr unsigned kParseFlags = pugi::parse_default | pugi::parse_comments | pugi::parse_declaration | pugi::parse_pi;

        constexpr StringView kNameStopCharacters = "=<>/\"'&";
        constexpr StringView kNameExtraCharacters = "-.";

        bool IsXmlSpace(char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        bool IsLineBreak(char c) {
            return c == '\r' || c == '\n';
        }

        /// FF FE and FE FF: the byte order marks of UTF-16 (and the first two bytes of UTF-32's)
        bool IsUtf16ByteOrderMark(unsigned char first, unsigned char second) {
            constexpr unsigned char kMarkHigh = 0xFF;
            constexpr unsigned char kMarkLow = 0xFE;
            return (first == kMarkHigh && second == kMarkLow) || (first == kMarkLow && second == kMarkHigh);
        }

        bool IsHighByte(char c) {
            return (static_cast<unsigned char>(c) & 0x80U) != 0;
        }

        bool IsNameStart(char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':' || IsHighByte(c);
        }

        bool IsNameCharacter(char c) {
            return IsNameStart(c) || (c >= '0' && c <= '9') || kNameExtraCharacters.find(c) != StringView::npos;
        }

        bool IsValidAttributeName(StringView name) {
            if (name.empty() || !IsNameStart(name.front())) {
                return false;
            }
            for (const char c : name) {
                if (!IsNameCharacter(c)) {
                    return false;
                }
            }
            return true;
        }

        /// One attribute of a start tag as written in the text; every offset is a byte offset into the text
        struct ScannedAttribute {
            /// End of the token before the attribute (the element's name or the previous closing quote)
            size_t gapBegin = 0;
            size_t nameBegin = 0;
            size_t nameEnd = 0;
            /// First byte after the opening quote
            size_t valueBegin = 0;
            /// The closing quote
            size_t valueEnd = 0;
            char quote = '"';
        };

        struct ScannedTag {
            size_t nameEnd = 0;
            Vector<ScannedAttribute> attributes;
        };

        struct Replacement {
            size_t begin = 0;
            size_t end = 0;
            String text;
        };

        size_t SkipSpace(StringView text, size_t pos) {
            while (pos < text.size() && IsXmlSpace(text[pos])) {
                ++pos;
            }
            return pos;
        }

        bool ScanStartTag(StringView text, size_t nameBegin, size_t nameLength, ScannedTag& tag, String& error) {
            size_t pos = nameBegin + nameLength;
            tag.nameEnd = pos;
            tag.attributes.clear();
            size_t gapBegin = pos;
            while (true) {
                const size_t tokenBegin = SkipSpace(text, pos);
                if (tokenBegin >= text.size()) {
                    error = "the start tag does not end";
                    return false;
                }
                const char c = text[tokenBegin];
                if (c == '>') {
                    return true;
                }
                if (c == '/') {
                    if (tokenBegin + 1 < text.size() && text[tokenBegin + 1] == '>') {
                        return true;
                    }
                    error = "a '/' in the start tag is not followed by '>'";
                    return false;
                }
                if (tokenBegin == pos) {
                    error = "an attribute does not follow whitespace";
                    return false;
                }

                ScannedAttribute attribute;
                attribute.gapBegin = gapBegin;
                attribute.nameBegin = tokenBegin;
                pos = tokenBegin;
                while (pos < text.size() && !IsXmlSpace(text[pos]) && kNameStopCharacters.find(text[pos]) == StringView::npos) {
                    ++pos;
                }
                attribute.nameEnd = pos;
                if (attribute.nameEnd == attribute.nameBegin) {
                    error = "an attribute has no name";
                    return false;
                }
                pos = SkipSpace(text, pos);
                if (pos >= text.size() || text[pos] != '=') {
                    error = "an attribute has no '='";
                    return false;
                }
                pos = SkipSpace(text, pos + 1);
                if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
                    error = "an attribute value is not quoted";
                    return false;
                }
                attribute.quote = text[pos];
                attribute.valueBegin = pos + 1;
                const size_t closing = text.find(attribute.quote, attribute.valueBegin);
                if (closing == StringView::npos) {
                    error = "an attribute value does not end";
                    return false;
                }
                attribute.valueEnd = closing;
                pos = closing + 1;
                gapBegin = pos;
                tag.attributes.push_back(attribute);
            }
        }

        /// Whether the start tag at @p offset is the one the document read for @p element
        bool MatchesDocument(StringView text, pugi::xml_node element, ScannedTag& tag, ptrdiff_t offset, String& error) {
            constexpr StringView kMismatch = "the text does not match the document: ";
            const StringView name = element.name();
            if (offset < 1 || static_cast<size_t>(offset) + name.size() > text.size()) {
                error = Format("{}the element '{}' is outside the text", kMismatch, name);
                return false;
            }
            const size_t nameBegin = static_cast<size_t>(offset);
            if (text[nameBegin - 1] != '<' || text.substr(nameBegin, name.size()) != name) {
                error = Format("{}no '<{}' at byte {}", kMismatch, name, nameBegin - 1);
                return false;
            }
            const size_t afterName = nameBegin + name.size();
            if (afterName >= text.size() || !(IsXmlSpace(text[afterName]) || text[afterName] == '/' || text[afterName] == '>')) {
                error = Format("{}the tag at byte {} is not '<{}'", kMismatch, nameBegin - 1, name);
                return false;
            }
            String scanError;
            if (!ScanStartTag(text, nameBegin, name.size(), tag, scanError)) {
                error = Format("{}'<{}': {}", kMismatch, name, scanError);
                return false;
            }
            size_t index = 0;
            for (const pugi::xml_attribute attribute : element.attributes()) {
                const StringView attributeName = attribute.name();
                const bool same = index < tag.attributes.size() && text.substr(tag.attributes[index].nameBegin, tag.attributes[index].nameEnd - tag.attributes[index].nameBegin) == attributeName;
                if (!same) {
                    error = Format("{}the attributes of '<{}>' differ at '{}'", kMismatch, name, attributeName);
                    return false;
                }
                ++index;
            }
            if (index != tag.attributes.size()) {
                error = Format("{}'<{}>' has more attributes in the text", kMismatch, name);
                return false;
            }
            return true;
        }

        void AppendCharacterReference(String& out, char c) {
            out.append(Format("&#{};", static_cast<int>(c)));
        }

        /// A value as it is written between @p quote characters; a control character that XML 1.0 forbids is refused
        bool AppendEscaped(String& out, StringView value, char quote, String& error) {
            for (const char c : value) {
                switch (c) {
                    case '&':
                        out.append("&amp;");
                        break;
                    case '<':
                        out.append("&lt;");
                        break;
                    case '\n':
                    case '\r':
                    case '\t':
                        AppendCharacterReference(out, c);
                        break;
                    default:
                        if (c == quote) {
                            out.append(quote == '"' ? "&quot;" : "&apos;");
                        } else if (static_cast<unsigned char>(c) < 0x20U) {
                            error = Format("a value holds the control character {}", static_cast<int>(c));
                            return false;
                        } else {
                            out.push_back(c);
                        }
                        break;
                }
            }
            return true;
        }

        const ScannedAttribute* FindScanned(StringView text, const ScannedTag& tag, StringView name) {
            for (const ScannedAttribute& attribute : tag.attributes) {
                if (text.substr(attribute.nameBegin, attribute.nameEnd - attribute.nameBegin) == name) {
                    return &attribute;
                }
            }
            return nullptr;
        }

        /// What goes between the last token and a new attribute: the line break and indentation of the last attribute
        /// when that one starts a line, a single space otherwise
        String AddSeparator(StringView text, const ScannedTag& tag) {
            if (tag.attributes.empty()) {
                return " ";
            }
            const ScannedAttribute& last = tag.attributes.back();
            const StringView gap = text.substr(last.gapBegin, last.nameBegin - last.gapBegin);
            for (size_t i = 0; i < gap.size(); ++i) {
                if (IsLineBreak(gap[i])) {
                    return String{gap.substr(i)};
                }
            }
            return " ";
        }

        bool BuildReplacements(StringView text, const XmlTextPatch::ElementEdit& edit, const ScannedTag& tag, Vector<Replacement>& replacements, String& error) {
            String added;
            const String separator = AddSeparator(text, tag);
            for (const XmlTextPatch::AttributeEdit& attributeEdit : edit.attributes) {
                const ScannedAttribute* scanned = FindScanned(text, tag, attributeEdit.name);
                if (!attributeEdit.value) {
                    if (scanned != nullptr) {
                        replacements.push_back({scanned->gapBegin, scanned->valueEnd + 1, String{}});
                    }
                    continue;
                }
                if (scanned != nullptr) {
                    Replacement replacement{scanned->valueBegin, scanned->valueEnd, String{}};
                    if (!AppendEscaped(replacement.text, *attributeEdit.value, scanned->quote, error)) {
                        return false;
                    }
                    replacements.push_back(eastl::move(replacement));
                    continue;
                }
                added.append(separator);
                added.append(attributeEdit.name);
                added.append("=\"");
                if (!AppendEscaped(added, *attributeEdit.value, '"', error)) {
                    return false;
                }
                added.push_back('"');
            }
            if (!added.empty()) {
                const size_t insertAt = tag.attributes.empty() ? tag.nameEnd : tag.attributes.back().valueEnd + 1;
                replacements.push_back({insertAt, insertAt, eastl::move(added)});
            }
            return true;
        }

        bool CheckAttributeEdits(const XmlTextPatch::ElementEdit& edit, String& error) {
            for (size_t i = 0; i < edit.attributes.size(); ++i) {
                const String& name = edit.attributes[i].name;
                if (!IsValidAttributeName(name)) {
                    error = Format("'{}' is not an attribute name", name);
                    return false;
                }
                for (size_t j = 0; j < i; ++j) {
                    if (edit.attributes[j].name == name) {
                        error = Format("the attribute '{}' of '<{}>' is edited twice", name, edit.element.name());
                        return false;
                    }
                }
            }
            return true;
        }

    }  // namespace

    bool XmlTextPatch::Parse(StringView text, pugi::xml_document& doc, String& error) {
        if (text.size() >= 2 && IsUtf16ByteOrderMark(static_cast<unsigned char>(text[0]), static_cast<unsigned char>(text[1]))) {
            error = "the text is UTF-16 or UTF-32, only UTF-8 is patched";
            return false;
        }
        if (text.find('\0') != StringView::npos) {
            error = "the text holds a NUL byte";
            return false;
        }
        doc.reset();
        const pugi::xml_parse_result result = doc.load_buffer(text.data(), text.size(), kParseFlags, pugi::encoding_utf8);
        if (!result) {
            error = Format("the text does not parse: {} at byte {}", result.description(), result.offset);
            return false;
        }
        return true;
    }

    bool XmlTextPatch::Apply(StringView text, Span<const ElementEdit> edits, String& out, String& error) {
        Vector<Replacement> replacements;
        Vector<ptrdiff_t> offsets;
        ScannedTag tag;
        for (const ElementEdit& edit : edits) {
            if (edit.element.type() != pugi::node_element) {
                error = "an edit names a node that is not an element";
                return false;
            }
            if (!CheckAttributeEdits(edit, error)) {
                return false;
            }
            const ptrdiff_t offset = edit.element.offset_debug();
            for (const ptrdiff_t seen : offsets) {
                if (seen == offset) {
                    error = Format("the element '<{}>' at byte {} is edited twice", edit.element.name(), offset);
                    return false;
                }
            }
            offsets.push_back(offset);
            if (!MatchesDocument(text, edit.element, tag, offset, error)) {
                return false;
            }
            if (!BuildReplacements(text, edit, tag, replacements, error)) {
                return false;
            }
        }

        eastl::sort(replacements.begin(), replacements.end(), [](const Replacement& a, const Replacement& b) { return a.begin != b.begin ? a.begin < b.begin : a.end < b.end; });
        String result;
        result.reserve(text.size());
        size_t copied = 0;
        for (const Replacement& replacement : replacements) {
            if (replacement.begin < copied) {
                error = "two edits touch the same bytes";
                return false;
            }
            result.append(text.data() + copied, replacement.begin - copied);
            result.append(replacement.text);
            copied = replacement.end;
        }
        result.append(text.data() + copied, text.size() - copied);
        out = eastl::move(result);
        return true;
    }

}  // namespace BECore
