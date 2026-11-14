#include "UIVars.h"

namespace BECore {

    void UIVars::Set(PoolString name, StringView value) {
        const auto it = _values.find(name);
        if (it == _values.end()) {
            _values.emplace(name, String(value.data(), value.size()));
            ++_version;
            return;
        }
        if (StringView(it->second) == value) {
            return;
        }
        it->second.assign(value.data(), value.size());
        ++_version;
    }

    StringView UIVars::Get(PoolString name) const {
        const auto it = _values.find(name);
        return it == _values.end() ? StringView{} : StringView(it->second);
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

    bool UIVars::HasPlaceholders(StringView text) {
        return text.find('{') != StringView::npos || text.find('}') != StringView::npos;
    }

    String UIVars::Expand(StringView text) const {
        String result;
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
                if (close == StringView::npos) {
                    result += c;  // unclosed: left as it is
                    continue;
                }
                const StringView name = text.substr(i + 1, close - i - 1);
                const StringView value = Get(PoolString::Intern(name));
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
