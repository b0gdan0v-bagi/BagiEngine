#include "WriteTargets.h"

#include <BECore/Values/Effects/ScriptIdentifier.h>

namespace BECore {

    namespace {

        [[nodiscard]] bool IsDottedPath(StringView path) {
            bool segmentStart = true;
            for (const char c : path) {
                if (c == '.') {
                    if (segmentStart) {
                        return false;
                    }
                    segmentStart = true;
                } else if (segmentStart ? IsScriptIdentifierStart(c) : IsScriptIdentifierPart(c)) {
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
