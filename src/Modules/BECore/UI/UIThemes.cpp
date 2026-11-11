#include "UIThemes.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIColor.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view ConfigName = "UIThemes";
        constexpr eastl::string_view ThemeElement = "theme";
        constexpr eastl::string_view ColorElement = "color";

        bool IsLetter(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        bool IsDigit(char c) {
            return c >= '0' && c <= '9';
        }

    }  // namespace

    void UIThemes::Initialize() {
        const ConfigManager& configs = CoreManager::GetConfigManager();
        if (!configs.HasConfig(ConfigName)) {
            return;
        }
        for (const eastl::string& problem : LoadFromXml(configs.GetConfig(ConfigName))) {
            LOG_WARNING("UIThemes: {}"_format(problem.c_str()));
        }
    }

    bool UIThemes::IsValidToken(eastl::string_view token) {
        if (token.empty() || !IsLetter(token.front())) {
            return false;
        }
        for (const char c : token) {
            if (!IsLetter(c) && !IsDigit(c) && c != '_' && c != '-') {
                return false;
            }
        }
        return true;
    }

    const UIThemes::Theme* UIThemes::FindTheme(PoolString theme) const {
        for (const Theme& candidate : _themes) {
            if (candidate.id == theme) {
                return &candidate;
            }
        }
        return nullptr;
    }

    std::optional<Color> UIThemes::FindColor(const Theme* theme, eastl::string_view token) {
        if (!theme) {
            return std::nullopt;
        }
        const auto pooledToken = PoolString::Find(token);
        if (!pooledToken) {
            return std::nullopt;
        }
        const auto it = theme->colors.find(*pooledToken);
        if (it == theme->colors.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool UIThemes::Set(PoolString theme, eastl::string_view token, const Color& color) {
        if (theme.Empty() || !IsValidToken(token)) {
            return false;
        }
        const PoolString pooledToken = PoolString::Intern(token);
        Theme* target = nullptr;
        for (Theme& candidate : _themes) {
            if (candidate.id == theme) {
                target = &candidate;
                break;
            }
        }
        if (!target) {
            if (_default.Empty()) {
                _default = theme;
            }
            _themes.push_back(Theme{theme, {}});
            _themeIds.push_back(theme);
            target = &_themes.back();
        }

        bool known = false;
        for (const PoolString name : _tokens) {
            if (name == pooledToken) {
                known = true;
                break;
            }
        }
        if (!known) {
            _tokens.push_back(pooledToken);
        }

        const auto it = target->colors.find(pooledToken);
        if (it == target->colors.end()) {
            target->colors.emplace(pooledToken, color);
        } else if (ColorsEqual(it->second, color)) {
            return true;
        } else {
            it->second = color;
        }
        ++_version;
        return true;
    }

    void UIThemes::SetDefaultTheme(PoolString theme) {
        if (_default == theme) {
            return;
        }
        _default = theme;
        ++_version;
    }

    bool UIThemes::SetTheme(PoolString theme) {
        if (!FindTheme(theme)) {
            return false;
        }
        if (GetTheme() != theme) {
            ++_version;
        }
        _current = theme;
        return true;
    }

    PoolString UIThemes::GetTheme() const {
        return _current.Empty() ? _default : _current;
    }

    bool UIThemes::HasTheme(PoolString theme) const {
        return FindTheme(theme) != nullptr;
    }

    std::optional<Color> UIThemes::Find(eastl::string_view token) const {
        const PoolString current = GetTheme();
        for (const PoolString theme : {current, _default}) {
            if (const auto color = FindColor(FindTheme(theme), token)) {
                return color;
            }
        }
        return std::nullopt;
    }

    void UIThemes::Resolve(UIColor& color) {
        if (!color.HasToken()) {
            return;
        }
        if (const auto found = Find(color.token.ToStringView())) {
            static_cast<Color&>(color) = *found;
            return;
        }
        static_cast<Color&>(color) = kMissingTokenColor;
        for (const PoolString missing : _missing) {
            if (missing == color.token) {
                return;
            }
        }
        _missing.push_back(color.token);
        LOG_WARNING("UIThemes: the token '@{}' is in no theme, drawn in magenta"_format(color.token.ToStringView()));
    }

    bool UIThemes::HasToken(PoolString theme, eastl::string_view token) const {
        return FindColor(FindTheme(theme), token).has_value();
    }

    void UIThemes::Clear() {
        _themes.clear();
        _themeIds.clear();
        _tokens.clear();
        _missing.clear();
        _default = PoolString{};
        _current = PoolString{};
        ++_version;
    }

    eastl::vector<eastl::string> UIThemes::LoadFromXml(const XmlNode& node) {
        eastl::vector<eastl::string> problems;
        if (!node) {
            return problems;
        }

        for (const XmlNode child : node.Children()) {
            const eastl::string_view name = child.Name();
            if (name.empty()) {
                continue;  // a comment or text
            }
            if (name != ThemeElement) {
                problems.push_back(Format("unknown element '{}'", name));
                continue;
            }
            const auto id = child.GetAttribute("id");
            if (!id || id->empty()) {
                problems.push_back("a theme has no id, its colours are skipped");
                continue;
            }
            const PoolString theme = PoolString::Intern(*id);
            size_t loaded = 0;
            for (const XmlNode entry : child.Children()) {
                const eastl::string_view entryName = entry.Name();
                if (entryName.empty()) {
                    continue;
                }
                if (entryName != ColorElement) {
                    problems.push_back(Format("theme '{}': unknown element '{}'", *id, entryName));
                    continue;
                }
                const auto token = entry.GetAttribute("name");
                if (!token || token->empty()) {
                    problems.push_back(Format("theme '{}': a colour has no name", *id));
                    continue;
                }
                if (!IsValidToken(*token)) {
                    problems.push_back(Format("theme '{}': '{}' is not a token name (letters, digits, '_' and '-', a letter first)", *id, *token));
                    continue;
                }
                const auto value = entry.GetAttribute("value");
                if (!value) {
                    problems.push_back(Format("theme '{}': the colour '{}' has no value", *id, *token));
                    continue;
                }
                const auto color = UIText::ParseColor(*value);
                if (!color) {
                    problems.push_back(Format("theme '{}': the colour '{}' has the value '{}', which is not a colour", *id, *token, *value));
                    continue;
                }
                if (HasToken(theme, *token)) {
                    problems.push_back(Format("theme '{}': the colour '{}' is set twice, the first stays", *id, *token));
                    continue;
                }
                Set(theme, *token, *color);
                ++loaded;
            }
            if (loaded == 0 && !HasTheme(theme)) {
                problems.push_back(Format("theme '{}' has no colours", *id));
            }
        }

        if (const auto defaultId = node.GetAttribute("default")) {
            const PoolString defaultTheme = PoolString::Intern(*defaultId);
            if (HasTheme(defaultTheme)) {
                SetDefaultTheme(defaultTheme);
            } else {
                problems.push_back(Format("the default theme '{}' has no colours", *defaultId));
            }
        }
        return problems;
    }

}  // namespace BECore
