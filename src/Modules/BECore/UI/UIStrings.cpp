#include "UIStrings.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UITidyMessage.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view ConfigName = "UIStrings";
        constexpr eastl::string_view LanguageElement = "language";
        constexpr eastl::string_view StringElement = "string";

        /// The end of the reference that starts at @p open (the position of its `{`), or npos when it is not one: not `{#`, unclosed, or empty (`{#}`)
        size_t ReferenceEnd(eastl::string_view text, size_t open) {
            if (open + 1 >= text.size() || text[open + 1] != '#') {
                return eastl::string_view::npos;
            }
            const size_t close = text.find('}', open + 2);
            if (close == eastl::string_view::npos || close == open + 2) {
                return eastl::string_view::npos;
            }
            return close;
        }

        /// The key is any text without `}`, and not empty
        bool IsValidKey(eastl::string_view key) {
            return !key.empty() && key.find('}') == eastl::string_view::npos;
        }

        eastl::string Describe(eastl::string_view what, eastl::string_view detail) {
            return Format("{}: {}", what, detail);
        }

    }  // namespace

    void UIStrings::Initialize() {
        const ConfigManager& configs = CoreManager::GetConfigManager();
        if (!configs.HasConfig(ConfigName)) {
            return;
        }
        for (const eastl::string& problem : LoadFromXml(configs.GetConfig(ConfigName))) {
            LOG_WARNING("UIStrings: {}"_format(problem.c_str()));
        }
    }

    const UIStrings::Language* UIStrings::FindLanguage(PoolString language) const {
        for (const Language& candidate : _languages) {
            if (candidate.id == language) {
                return &candidate;
            }
        }
        return nullptr;
    }

    const eastl::string* UIStrings::FindString(const Language* language, eastl::string_view key) const {
        if (!language) {
            return nullptr;
        }
        const auto pooledKey = PoolString::Find(key);
        if (!pooledKey) {
            return nullptr;
        }
        const auto it = language->strings.find(*pooledKey);
        return it == language->strings.end() ? nullptr : &it->second;
    }

    bool UIStrings::Set(PoolString language, eastl::string_view key, eastl::string_view text) {
        if (language.Empty() || !IsValidKey(key)) {
            return false;
        }
        const PoolString pooledKey = PoolString::Intern(key);
        Language* target = nullptr;
        for (Language& candidate : _languages) {
            if (candidate.id == language) {
                target = &candidate;
                break;
            }
        }
        if (!target) {
            if (_default.Empty()) {
                _default = language;
            }
            _languages.push_back(Language{language, {}});
            _languageIds.push_back(language);
            target = &_languages.back();
        }

        const auto it = target->strings.find(pooledKey);
        if (it == target->strings.end()) {
            target->strings.emplace(pooledKey, eastl::string(text.data(), text.size()));
        } else if (eastl::string_view(it->second.data(), it->second.size()) == text) {
            return true;
        } else {
            it->second.assign(text.data(), text.size());
        }
        ++_version;
        return true;
    }

    void UIStrings::SetDefaultLanguage(PoolString language) {
        if (_default == language) {
            return;
        }
        _default = language;
        ++_version;
    }

    bool UIStrings::SetLanguage(PoolString language) {
        if (!FindLanguage(language)) {
            return false;
        }
        if (GetLanguage() != language) {
            ++_version;
        }
        _current = language;
        return true;
    }

    PoolString UIStrings::GetLanguage() const {
        return _current.Empty() ? _default : _current;
    }

    bool UIStrings::HasLanguage(PoolString language) const {
        return FindLanguage(language) != nullptr;
    }

    Optional<eastl::string_view> UIStrings::Find(eastl::string_view key) const {
        const PoolString current = GetLanguage();
        for (const PoolString language : {current, _default}) {
            if (const eastl::string* text = FindString(FindLanguage(language), key)) {
                return eastl::string_view(text->data(), text->size());
            }
        }
        return std::nullopt;
    }

    bool UIStrings::HasReferences(eastl::string_view text) {
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '{') {
                continue;
            }
            if (i + 1 < text.size() && text[i + 1] == '{') {
                ++i;  // `{{` is a literal brace, and a `#` after it is not a reference
                continue;
            }
            if (ReferenceEnd(text, i) != eastl::string_view::npos) {
                return true;
            }
        }
        return false;
    }

    void UIStrings::RecordMissing(eastl::string_view key) {
        const PoolString language = GetLanguage();
        for (const MissingString& missing : _missing) {
            if (missing.language == language && eastl::string_view(missing.key.data(), missing.key.size()) == key) {
                return;
            }
        }
        _missing.push_back(MissingString{language, eastl::string(key.data(), key.size())});
    }

    eastl::string UIStrings::Resolve(eastl::string_view text) {
        eastl::string result;
        result.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c != '{') {
                result += c;
                continue;
            }
            if (i + 1 < text.size() && text[i + 1] == '{') {
                result.append("{{");  // left for the variable step
                ++i;
                continue;
            }
            const size_t close = ReferenceEnd(text, i);
            if (close == eastl::string_view::npos) {
                result += c;
                continue;
            }
            const eastl::string_view key = text.substr(i + 2, close - i - 2);
            if (const auto found = Find(key)) {
                result.append(found->data(), found->size());
            } else {
                RecordMissing(key);
                result.append(key.data(), key.size());
            }
            i = close;
        }
        return result;
    }

    void UIStrings::Clear() {
        _languages.clear();
        _languageIds.clear();
        _missing.clear();
        _default = PoolString{};
        _current = PoolString{};
        ++_version;
    }

    eastl::vector<eastl::string> UIStrings::LoadFromXml(const XmlNode& node) {
        eastl::vector<eastl::string> problems;
        if (!node) {
            return problems;
        }

        for (const XmlNode child : node.Children()) {
            const eastl::string_view name = child.Name();
            if (name.empty()) {
                continue;  // a comment or text
            }
            if (name != LanguageElement) {
                problems.push_back(Describe("unknown element", name));
                continue;
            }
            const auto id = child.GetAttribute("id");
            if (!id || id->empty()) {
                problems.push_back(Describe("a language has no id", "its strings are skipped"));
                continue;
            }
            const PoolString language = PoolString::Intern(*id);
            size_t loaded = 0;
            for (const XmlNode entry : child.Children()) {
                const eastl::string_view entryName = entry.Name();
                if (entryName.empty()) {
                    continue;
                }
                if (entryName != StringElement) {
                    problems.push_back(Format("language '{}': unknown element '{}'", *id, entryName));
                    continue;
                }
                const auto key = entry.GetAttribute("key");
                if (!key || key->empty()) {
                    problems.push_back(Format("language '{}': a string has no key", *id));
                    continue;
                }
                if (!IsValidKey(*key)) {
                    problems.push_back(Format("language '{}': the key '{}' has a '}}'", *id, *key));
                    continue;
                }
                const auto text = entry.GetAttribute("text");
                if (!text) {
                    problems.push_back(Format("language '{}': the string '{}' has no text", *id, *key));
                    continue;
                }
                const PoolString pooledKey = PoolString::Intern(*key);
                const Language* existing = FindLanguage(language);
                if (existing && existing->strings.find(pooledKey) != existing->strings.end()) {
                    problems.push_back(Format("language '{}': the key '{}' is set twice, the first stays", *id, *key));
                    continue;
                }
                Set(language, *key, *text);
                ++loaded;
            }
            if (loaded == 0 && !HasLanguage(language)) {
                problems.push_back(Format("language '{}' has no strings", *id));
            }
        }

        if (const auto defaultId = node.GetAttribute("default")) {
            const PoolString defaultLanguage = PoolString::Intern(*defaultId);
            if (HasLanguage(defaultLanguage)) {
                SetDefaultLanguage(defaultLanguage);
            } else {
                problems.push_back(Format("the default language '{}' has no strings", *defaultId));
            }
        }
        // The messages repeat ids, keys and values of the file
        for (eastl::string& problem : problems) {
            problem = UIText::TidyMessage(eastl::string_view(problem.data(), problem.size()));
        }
        return problems;
    }

}  // namespace BECore
