#include "UIVars.h"

namespace BECore {

    void UIVars::Set(PoolString name, eastl::string_view value) {
        const auto it = _values.find(name);
        if (it == _values.end()) {
            _values.emplace(name, eastl::string(value.data(), value.size()));
            ++_version;
            return;
        }
        if (eastl::string_view(it->second.data(), it->second.size()) == value) {
            return;
        }
        it->second.assign(value.data(), value.size());
        ++_version;
    }

    eastl::string_view UIVars::Get(PoolString name) const {
        const auto it = _values.find(name);
        return it == _values.end() ? eastl::string_view{} : eastl::string_view(it->second.data(), it->second.size());
    }

    bool UIVars::Has(PoolString name) const {
        return _values.find(name) != _values.end();
    }

    bool UIVars::Remove(PoolString name) {
        if (_values.erase(name) == 0) {
            return false;
        }
        ++_version;
        return true;
    }

    void UIVars::Clear() {
        if (!_values.empty()) {
            _values.clear();
        }
        ++_version;
    }

    bool UIVars::HasPlaceholders(eastl::string_view text) {
        return text.find('{') != eastl::string_view::npos || text.find('}') != eastl::string_view::npos;
    }

    eastl::string UIVars::Expand(eastl::string_view text) const {
        eastl::string result;
        result.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '{') {
                if (i + 1 < text.size() && text[i + 1] == '{') {
                    result += '{';
                    ++i;
                    continue;
                }
                const size_t close = text.find('}', i + 1);
                if (close == eastl::string_view::npos) {
                    result += c;  // unclosed: left as it is
                    continue;
                }
                const eastl::string_view name = text.substr(i + 1, close - i - 1);
                const eastl::string_view value = Get(PoolString::Intern(name));
                result.append(value.data(), value.size());
                i = close;
            } else if (c == '}' && i + 1 < text.size() && text[i + 1] == '}') {
                result += '}';
                ++i;
            } else {
                result += c;
            }
        }
        return result;
    }

}  // namespace BECore
