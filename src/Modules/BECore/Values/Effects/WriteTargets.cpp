#include "WriteTargets.h"


namespace BECore {

    namespace {

        [[nodiscard]] bool IsIdentifierStart(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        [[nodiscard]] bool IsIdentifierPart(char c) {
            return IsIdentifierStart(c) || (c >= '0' && c <= '9');
        }

        [[nodiscard]] bool IsDottedPath(StringView path) {
            bool segmentStart = true;
            for (const char c : path) {
                if (c == '.') {
                    if (segmentStart) {
                        return false;
                    }
                    segmentStart = true;
                } else if (segmentStart ? IsIdentifierStart(c) : IsIdentifierPart(c)) {
                    segmentStart = false;
                } else {
                    return false;
                }
            }
            return !segmentStart;
        }

    }  // namespace

    bool WriteTargets::Allow(StringView path) {
        if (!IsDottedPath(path)) {
            return false;
        }
        const bool known = std::any_of(_paths.begin(), _paths.end(), [path](const String& allowed) { return StringView(allowed) == path; });
        if (!known) {
            _paths.push_back(String(path));
        }
        return true;
    }

    bool WriteTargets::Permits(StringView path) const {
        for (const String& allowed : _paths) {
            if (path.size() < allowed.size() || path.substr(0, allowed.size()) != StringView(allowed)) {
                continue;
            }
            if (path.size() == allowed.size() || path[allowed.size()] == '.') {
                return true;
            }
        }
        return false;
    }

}  // namespace BECore
