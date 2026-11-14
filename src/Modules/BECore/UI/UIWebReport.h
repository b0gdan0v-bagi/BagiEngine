#pragma once

#include <BECore/UI/UITidyMessage.h>

namespace BECore {

    /// One thing the web-style front-end could not read or apply
    struct UIWebIssue {
        eastl::string source;  ///< where it was read: "html", "css" or "css:<href>" (an href is cut as a message is)
        uint32_t line = 0;     ///< 1-based; 0 when it is not about one line
        eastl::string message;
    };

    /**
     * @brief What a compile of the web-style front-end did and what it left out.
     *
     * Nothing the compile cannot apply is silent: the property, selector, tag or value is an issue, and the rest of the screen is still built. The counters
     * are the numbers of the decision record ("CSS reach", `src/Modules/BECore/UI/AuthoringDecision.md`).
     */
    struct UIWebReport {
        eastl::vector<UIWebIssue> issues;
        uint32_t elements = 0;                 ///< elements written
        uint32_t rules = 0;                    ///< rules of the style sheets that were read
        uint32_t mediaBlocks = 0;              ///< `@media` blocks that were read
        uint32_t declarations = 0;             ///< declarations of those rules
        uint32_t declarationsUnsupported = 0;  ///< declarations whose property or value is outside the subset
        uint32_t variants = 0;                 ///< variants written
        uint32_t issuesDropped = 0;            ///< issues past kMaxIssues that were not stored

        /// The most issues a report keeps. A message repeats text of the file (a selector, a value, an href) and is cut by UIText::TidyMessage, and the check for a repeat is a
        /// scan of the list
        static constexpr size_t kMaxIssues = 200;
        static constexpr size_t kMaxMessageBytes = UIText::kMaxMessageBytes;

        /// Adds an issue (its message and source go through UIText::TidyMessage). @return false (and adds nothing) when the same issue is already there, so a rule that matches many elements reports
        /// once. Past kMaxIssues the list gets one last issue that says so, and a new issue is counted in issuesDropped and not stored (true: it is not known to be a repeat)
        bool AddOnce(StringView source, uint32_t line, eastl::string message) {
            const eastl::string tidySource = UIText::TidyMessage(source);
            const eastl::string tidyMessage = UIText::TidyMessage(StringView(message));
            for (const UIWebIssue& issue : issues) {
                if (issue.line == line && issue.message == tidyMessage && issue.source == tidySource) {
                    return false;
                }
            }
            if (issues.size() > kMaxIssues) {
                ++issuesDropped;
                return true;
            }
            if (issues.size() == kMaxIssues) {
                issues.push_back({"html", 0, Format("more issues follow: a report lists the first {} and not the rest", kMaxIssues)});
                ++issuesDropped;
                return true;
            }
            issues.push_back({tidySource, line, tidyMessage});
            return true;
        }

        [[nodiscard]] bool HasIssues() const {
            return !issues.empty();
        }
    };

}  // namespace BECore
