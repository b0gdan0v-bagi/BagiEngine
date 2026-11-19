#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzTargets.h>

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
     * Only the XML of a document the report calls clean is promised to be well-formed: a document with issues may give XML that the loader refuses, which is how a damaged
     * file is refused.
     */
    void FuzzWebUi(const uint8_t* data, size_t size) {
        const auto* begin = reinterpret_cast<const char*>(data);
        const char* separator = size == 0 ? nullptr : static_cast<const char*>(std::memchr(begin, '\0', size));
        const size_t htmlSize = separator != nullptr ? static_cast<size_t>(separator - begin) : size;
        const String html(begin, htmlSize);
        const String css = separator != nullptr ? String(separator + 1, size - htmlSize - 1) : String();

        const UIWebCompiler::StylesheetLoader loader = [&css](StringView) { return Optional<String>(css); };

        UIWebReport report;
        const String xml = UIWebCompiler::Compile(StringView(html), loader, report);
        UIWebReport again;
        const String xmlAgain = UIWebCompiler::Compile(StringView(html), loader, again);
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
