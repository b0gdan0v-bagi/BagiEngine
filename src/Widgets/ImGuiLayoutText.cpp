#include "ImGuiLayoutText.h"


namespace BECore {

    namespace {

        struct LegacyWindowName {
            StringView legacy;
            StringView current;
        };

        constexpr LegacyWindowName kLegacyWindowNames[] = {
            {"Config Editor", "ConfigEditor"},
        };

        constexpr StringView kWindowHeaderPrefix = "[Window][";
        constexpr StringView kDockIdKey = "DockId=";

        /// One `[...]` section: from its header line to the next header line (or the end), trailing blank lines included
        struct Section {
            size_t begin = 0;
            size_t end = 0;
            StringView windowName;  // empty unless the header is `[Window][<name>]`
            bool hasDockId = false;
        };

        CORE_ENUM(SectionAction, uint8_t, Keep, Rename, Drop)

        StringView WindowNameOfHeader(StringView line) {
            while (line.ends_with('\r') || line.ends_with('\n')) {
                line.remove_suffix(1);
            }
            if (!line.starts_with(kWindowHeaderPrefix) || !line.ends_with(']')) {
                return {};
            }
            line.remove_prefix(kWindowHeaderPrefix.size());
            line.remove_suffix(1);
            return line;
        }

        Vector<Section> SplitSections(StringView text) {
            Vector<Section> sections;
            size_t position = 0;
            while (position < text.size()) {
                const size_t lineEnd = text.find('\n', position);
                const size_t next = lineEnd == StringView::npos ? text.size() : lineEnd + 1;
                const StringView line = text.substr(position, next - position);
                if (line.starts_with('[')) {
                    if (!sections.empty()) {
                        sections.back().end = position;
                    }
                    Section section;
                    section.begin = position;
                    section.windowName = WindowNameOfHeader(line);
                    sections.push_back(section);
                } else if (!sections.empty() && line.starts_with(kDockIdKey)) {
                    sections.back().hasDockId = true;
                }
                position = next;
            }
            if (!sections.empty()) {
                sections.back().end = text.size();
            }
            return sections;
        }

        /// The text with the section of @p name renamed as the rules of MigrateLegacyWindowNames say; equal to @p text when none applies
        String MigrateOne(StringView text, const LegacyWindowName& name) {
            const Vector<Section> sections = SplitSections(text);
            Vector<SectionAction> actions(sections.size(), SectionAction::Keep);

            bool anyLegacyDocked = false;
            bool anyCurrent = false;
            for (const Section& section : sections) {
                if (section.windowName == name.legacy && section.hasDockId) {
                    anyLegacyDocked = true;
                } else if (section.windowName == name.current) {
                    anyCurrent = true;
                }
            }

            bool changed = false;
            for (size_t i = 0; i < sections.size(); ++i) {
                const Section& section = sections[i];
                if (section.windowName == name.legacy && (section.hasDockId || !anyCurrent)) {
                    actions[i] = SectionAction::Rename;
                    changed = true;
                } else if (section.windowName == name.current && anyLegacyDocked) {
                    actions[i] = SectionAction::Drop;
                    changed = true;
                }
            }
            if (!changed) {
                return String(text.data(), text.size());
            }

            String result;
            result.reserve(text.size());
            const size_t preambleEnd = sections.empty() ? text.size() : sections.front().begin;
            result.append(text.data(), preambleEnd);
            for (size_t i = 0; i < sections.size(); ++i) {
                const Section& section = sections[i];
                const StringView body = text.substr(section.begin, section.end - section.begin);
                if (actions[i] == SectionAction::Drop) {
                    continue;
                }
                if (actions[i] == SectionAction::Keep) {
                    result.append(body.data(), body.size());
                    continue;
                }
                result.append(kWindowHeaderPrefix.data(), kWindowHeaderPrefix.size());
                result.append(name.current.data(), name.current.size());
                const StringView afterName = body.substr(kWindowHeaderPrefix.size() + name.legacy.size());
                result.append(afterName.data(), afterName.size());
            }
            return result;
        }

    }  // namespace

    String MigrateLegacyWindowNames(StringView iniText) {
        String text(iniText.data(), iniText.size());
        for (const LegacyWindowName& name : kLegacyWindowNames) {
            text = MigrateOne(StringView(text.data(), text.size()), name);
        }
        return text;
    }

}  // namespace BECore
