// What compiling a web-style screen costs on the heap, measured from outside: the whole `UIWebCompiler::Compile`, the HTML parse and the CSS parse alone, and the
// rest (cascade, tree, variants, output text) as the whole minus those two. A benchmark run by hand, not a gate; the numbers decide whether the compiler's own
// containers are worth moving onto a scratch scope.

#include <BECore/UI/UICssParser.h>
#include <BECore/UI/UIHtmlParser.h>
#include <BECore/UI/UIWebCompiler.h>
#include <BECore/UIScreenFixtures.h>
#include <Common/AllocCheck.h>
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

namespace BECore::Tests {

    namespace {

        /// Copies of one screen in a document: a couple of hundred nodes, as a card list reaches. Twice as many go over the cascade's work bound (`UIWebCompiler::kMaxCascadeWork`), because
        /// every copy brings its own rules
        constexpr int kRepeats = 8;
        constexpr int kWarmRuns = 5;
        constexpr StringView kIdAttribute = "id=\"";

        struct Screen {
            const char* name;
            const char* html;
            const char* css;
        };

        constexpr Screen kScreens[] = {{"Settings", kSettingsHtml, kSettingsCss}, {"Selector", kSelectorHtml, kSelectorCss}};

        struct Sample {
            uint64_t allocs = 0;
            uint64_t bytes = 0;
            double ms = 0.0;
        };

        struct Sources {
            String html;
            String css;
            String joined;  ///< what `Compile` is given: `<style>` and the markup
        };

        bool IsNameChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }

        /// The ids of the HTML get `_<copy>` after them, as do the `#id` selectors of the CSS, so that no id repeats and no rule matches another copy
        String SuffixIds(StringView html, int copy) {
            const String suffix = Format("_{}", copy);
            String out;
            size_t at = 0;
            while (at < html.size()) {
                const size_t found = html.find(kIdAttribute, at);
                const bool attribute = found != StringView::npos && (found == 0 || html[found - 1] == ' ' || html[found - 1] == '\n');
                if (!attribute) {
                    out.append(html.data() + at, html.size() - at);
                    break;
                }
                const size_t nameStart = found + kIdAttribute.size();
                const size_t nameEnd = html.find('"', nameStart);
                out.append(html.data() + at, nameEnd - at);
                out += suffix;
                at = nameEnd;
            }
            return out;
        }

        String SuffixSelectors(StringView css, int copy) {
            const String suffix = Format("_{}", copy);
            String out;
            for (size_t i = 0; i < css.size(); ++i) {
                out += css[i];
                if (css[i] == '#') {
                    while (i + 1 < css.size() && IsNameChar(css[i + 1])) {
                        out += css[++i];
                    }
                    out += suffix;
                }
            }
            return out;
        }

        Sources Build(const Screen& screen, int repeats) {
            Sources sources;
            if (repeats <= 1) {
                sources.html = screen.html;
                sources.css = screen.css;
            }
            for (int copy = 0; copy < repeats && repeats > 1; ++copy) {
                sources.html += SuffixIds(screen.html, copy);
                sources.css += SuffixSelectors(screen.css, copy);
            }
            sources.joined = Format("<style>{}</style>{}", sources.css, sources.html);
            return sources;
        }

        template <typename TBody>
        Sample Measure(TBody&& body) {
            const auto start = std::chrono::steady_clock::now();
            const AllocCount counts = CountAllocations(body);
            const auto stop = std::chrono::steady_clock::now();
            return {counts.count, counts.bytes, std::chrono::duration<double, std::milli>(stop - start).count()};
        }

        struct CompileRun {
            Sample whole;
            Sample html;
            Sample css;
            size_t outputBytes = 0;
            uint32_t elements = 0;
            size_t issues = 0;
            size_t htmlNodes = 0;
        };

        CompileRun RunOnce(const Sources& sources) {
            CompileRun run;
            {
                UIWebReport report;
                String output;
                run.whole = Measure([&] { output = UIWebCompiler::Compile(sources.joined, {}, report); });
                run.outputBytes = output.size();
                run.elements = report.elements;
                run.issues = report.issues.size();
            }
            {
                UIWebReport report;
                UIHtmlDocument document;
                run.html = Measure([&] { document = UIHtmlParser::Parse(sources.joined, report); });
                run.htmlNodes = document.nodes.size();
            }
            {
                UIWebReport report;
                UICssSheet sheet;
                run.css = Measure([&] { UICssParser::ParseInto(sheet, sources.css, "css", 1, report); });
            }
            return run;
        }

        Sample Median(Vector<Sample> samples) {
            std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) { return a.ms < b.ms; });
            return samples[samples.size() / 2];
        }

        Sample Rest(const Sample& whole, const Sample& html, const Sample& css) {
            return {whole.allocs - html.allocs - css.allocs, whole.bytes - html.bytes - css.bytes, whole.ms - html.ms - css.ms};
        }

        void Print(const char* screen, const char* phase, const Sample& sample, const char* extra = "") {
            std::cout << "UIWebCompileBench " << screen << " " << phase << " allocs=" << sample.allocs << " bytes=" << sample.bytes << " ms=" << sample.ms << extra << "\n";
        }

        void PrintScreen(const String& name, const Sources& sources) {
            const CompileRun cold = RunOnce(sources);
            Vector<Sample> whole, html, css;
            CompileRun last;
            for (int i = 0; i < kWarmRuns; ++i) {
                last = RunOnce(sources);
                whole.push_back(last.whole);
                html.push_back(last.html);
                css.push_back(last.css);
            }
            const Sample warmWhole = Median(whole);
            const Sample warmHtml = Median(html);
            const Sample warmCss = Median(css);
            const String tail = Format(" htmlNodes={} elements={} outputBytes={} htmlBytes={} cssBytes={}", last.htmlNodes, last.elements, last.outputBytes, sources.html.size(), sources.css.size());
            Print(name.c_str(), "cold.whole", cold.whole, tail.c_str());
            Print(name.c_str(), "cold.html", cold.html);
            Print(name.c_str(), "cold.css", cold.css);
            Print(name.c_str(), "cold.rest", Rest(cold.whole, cold.html, cold.css));
            Print(name.c_str(), "warm.whole", warmWhole, tail.c_str());
            Print(name.c_str(), "warm.html", warmHtml);
            Print(name.c_str(), "warm.css", warmCss);
            Print(name.c_str(), "warm.rest", Rest(warmWhole, warmHtml, warmCss));
        }

    }  // namespace

    /// The repeated screen is a real compile, so the benchmark below does not measure an error path
    TEST(UIWebCompileBenchTest, RepeatedScreensCompileWithoutIssues) {
        for (const Screen& screen : kScreens) {
            const Sources sources = Build(screen, kRepeats);
            UIWebReport report;
            const String output = UIWebCompiler::Compile(sources.joined, {}, report);

            EXPECT_FALSE(output.empty()) << screen.name;
            EXPECT_TRUE(report.issues.empty()) << screen.name << ": " << (report.issues.empty() ? "" : report.issues[0].message.c_str());
            const CompileRun single = RunOnce(Build(screen, 1));
            EXPECT_EQ(report.elements, single.elements * kRepeats) << screen.name;
        }
    }

    /// A benchmark, not a check: the counts are zero without the profile preset (`kNoAllocTracker`)
    TEST(UIWebCompileBenchTest, DISABLED_CompileOfAScreen) {
        for (const Screen& screen : kScreens) {
            PrintScreen(String(screen.name), Build(screen, 1));
            PrintScreen(Format("{}x{}", screen.name, kRepeats), Build(screen, kRepeats));
        }
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

}  // namespace BECore::Tests
