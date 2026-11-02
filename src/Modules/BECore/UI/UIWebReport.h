#pragma once

namespace BECore {

    /// One thing the web-style front-end could not read or apply
    struct UIWebIssue {
        eastl::string source;  ///< where it was read: "html", "css" or "css:<href>"
        uint32_t line = 0;     ///< 1-based; 0 when it is not about one line
        eastl::string message;
    };

    /**
     * @brief What a compile of the web-style front-end did and what it left out.
     *
     * Nothing the compile cannot apply is silent: the property, selector, tag or value is an issue, and the rest of the screen is still built. The counters
     * are the numbers of the decision record ("CSS reach", `.claude/plans/ui-authoring-decision.md`).
     */
    struct UIWebReport {
        eastl::vector<UIWebIssue> issues;
        uint32_t elements = 0;                 ///< elements written
        uint32_t rules = 0;                    ///< rules of the style sheets that were read
        uint32_t mediaBlocks = 0;              ///< `@media` blocks that were read
        uint32_t declarations = 0;             ///< declarations of those rules
        uint32_t declarationsUnsupported = 0;  ///< declarations whose property or value is outside the subset
        uint32_t variants = 0;                 ///< variants written

        /// Adds an issue. @return false (and adds nothing) when the same issue is already there, so a rule that matches many elements reports once
        bool AddOnce(eastl::string_view source, uint32_t line, eastl::string message) {
            for (const UIWebIssue& issue : issues) {
                if (issue.line == line && issue.message == message && eastl::string_view(issue.source) == source) {
                    return false;
                }
            }
            issues.push_back({eastl::string(source), line, std::move(message)});
            return true;
        }

        [[nodiscard]] bool HasIssues() const {
            return !issues.empty();
        }
    };

}  // namespace BECore
