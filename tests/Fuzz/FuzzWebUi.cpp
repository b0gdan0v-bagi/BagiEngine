#include "FuzzTargets.h"

#include <BECore/Config/XmlNode.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UIWebCompiler.h>

namespace BECore::Fuzz {

    namespace {

        /// The sources a report names: the document, a `<style>` or `style=""` (css), and a linked style sheet (css:<href>)
        constexpr StringView kSourceHtml = "html";
        constexpr StringView kSourceCss = "css";
        constexpr StringView kSourceLinked = "css:";

        /// Lines of a text as the front end counts them: 1 and one more for every line feed
        uint32_t CountLines(StringView text) {
            uint32_t lines = 1;
            for (const char c : text) {
                if (c == '\n') {
                    ++lines;
                }
            }
            return lines;
        }

        /// Whether @p text has a control character (a line break included): a log line made of it is more than one line
        bool HasControlCharacter(StringView text) {
            for (const char c : text) {
                const auto byte = static_cast<uint8_t>(c);
                if (byte < 0x20 || byte == 0x7F) {
                    return true;
                }
            }
            return false;
        }

        bool SameReport(const UIWebReport& a, const UIWebReport& b) {
            if (a.issues.size() != b.issues.size() || a.elements != b.elements || a.rules != b.rules || a.mediaBlocks != b.mediaBlocks || a.declarations != b.declarations ||
                a.declarationsUnsupported != b.declarationsUnsupported || a.variants != b.variants) {
                return false;
            }
            for (size_t i = 0; i < a.issues.size(); ++i) {
                if (a.issues[i].source != b.issues[i].source || a.issues[i].line != b.issues[i].line || a.issues[i].message != b.issues[i].message) {
                    return false;
                }
            }
            return true;
        }

        void CheckReport(const UIWebReport& report, uint32_t lineBound) {
            FUZZ_CHECK(report.issues.size() <= UIWebReport::kMaxIssues + 1);
            for (const UIWebIssue& issue : report.issues) {
                const StringView source(issue.source.data(), issue.source.size());
                FUZZ_CHECK(source == kSourceHtml || source == kSourceCss || source.starts_with(kSourceLinked));
                FUZZ_CHECK(source.size() <= UIWebReport::kMaxMessageBytes);
                FUZZ_CHECK(issue.line <= lineBound);
                FUZZ_CHECK(!issue.message.empty());
                FUZZ_CHECK(issue.message.size() <= UIWebReport::kMaxMessageBytes);
                FUZZ_CHECK(!HasControlCharacter(StringView(issue.message)));
            }
        }

    }  // namespace

    /**
     * The input is `html, NUL, css` (no NUL: all of it is the html) and `Compile` gets a loader that gives the css part for any `href`.
     *
     * What is checked: `Compile` returns, and a second call gives the same text and the same report; every issue names a known source, a line inside the input,
     * and a short message with no control character; the text of a document the report calls clean is well-formed XML (a document with issues may give XML
     * that the loader refuses, which is how a damaged file is refused: nothing is promised for it); and the XML goes through `XmlDeserializer` and
     * `UIScreenContent::Deserialize`, the way `UIDocument` reads it.
     */
    void FuzzWebUi(const uint8_t* data, size_t size) {
        const auto* begin = reinterpret_cast<const char*>(data);
        const char* separator = size == 0 ? nullptr : static_cast<const char*>(std::memchr(begin, '\0', size));
        const size_t htmlSize = separator != nullptr ? static_cast<size_t>(separator - begin) : size;
        const eastl::string html(begin, htmlSize);
        const eastl::string css = separator != nullptr ? eastl::string(separator + 1, size - htmlSize - 1) : eastl::string();

        const UIWebCompiler::StylesheetLoader loader = [&css](StringView) { return Optional<eastl::string>(css); };

        UIWebReport report;
        const eastl::string xml = UIWebCompiler::Compile(StringView(html), loader, report);
        UIWebReport again;
        const eastl::string xmlAgain = UIWebCompiler::Compile(StringView(html), loader, again);
        FUZZ_CHECK(xml == xmlAgain);
        FUZZ_CHECK(SameReport(report, again));
        CheckReport(report, std::max(CountLines(StringView(html)), CountLines(StringView(css))));

        pugi::xml_document document;
        const bool wellFormed = document.load_buffer(xml.data(), xml.size());
        if (!report.HasIssues()) {
            FUZZ_CHECK(wellFormed);
        }

        XmlDeserializer deserializer;
        if (deserializer.LoadFromString(StringView(xml))) {
            UIScreenContent content;
            content.Deserialize(deserializer);
        }
    }

}  // namespace BECore::Fuzz
