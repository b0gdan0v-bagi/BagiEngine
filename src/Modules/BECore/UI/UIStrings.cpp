#include "UIStrings.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UITidyMessage.h>

namespace BECore {

    namespace {

        constexpr StringView ConfigName = "UIStrings";
        constexpr StringView LanguageElement = "language";
        constexpr StringView StringElement = "string";

        /// The end of the reference that starts at @p open (the position of its `{`), or npos when it is not one: not `{#`, unclosed, or empty (`{#}`)
        size_t ReferenceEnd(StringView text, size_t open) {
            if (open + 1 >= text.size() || text[open + 1] != '#') {
                return StringView::npos;
            }
            const size_t close = text.find('}', open + 2);
            if (close == StringView::npos || close == open + 2) {
                return StringView::npos;
            }
            return close;
        }

        /// The key is any text without `}`, and not empty
        bool IsValidKey(StringView key) {
            return !key.empty() && key.find('}') == StringView::npos;
        }

        String Describe(StringView what, StringView detail) {
            return Format("{}: {}", what, detail);
        }

    }  // namespace

    void UIStrings::Initialize() {
        const ConfigManager& configs = CoreManager::GetConfigManager();
        if (!configs.HasConfig(ConfigName)) {
            return;
        }
        for (const String& problem : LoadFromXml(configs.GetConfig(ConfigName))) {
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

    const String* UIStrings::FindString(const Language* language, StringView key) const {
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

    bool UIStrings::Set(PoolString language, StringView key, StringView text) {
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
            target->strings.emplace(pooledKey, String(text.data(), text.size()));
        } else if (StringView(it->second) == text) {
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

    Optional<StringView> UIStrings::Find(StringView key) const {
        const PoolString current = GetLanguage();
        for (const PoolString language : {current, _default}) {
            if (const String* text = FindString(FindLanguage(language), key)) {
                return StringView(text->data(), text->size());
            }
        }
        return std::nullopt;
    }

    bool UIStrings::HasReferences(StringView text) {
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '{') {
                continue;
            }
            if (i + 1 < text.size() && text[i + 1] == '{') {
                ++i;  // `{{` is a literal brace, and a `#` after it is not a reference
                continue;
            }
            if (ReferenceEnd(text, i) != StringView::npos) {
                return true;
            }
        }
        return false;
    }

    void UIStrings::RecordMissing(StringView key) {
        const PoolString language = GetLanguage();
        for (const MissingString& missing : _missing) {
            if (missing.language == language && StringView(missing.key) == key) {
                return;
            }
        }
        _missing.push_back(MissingString{language, String(key.data(), key.size())});
    }

    String UIStrings::Resolve(StringView text) {
        String result;
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
            if (close == StringView::npos) {
                result += c;
                continue;
            }
            const StringView key = text.substr(i + 2, close - i - 2);
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

    eastl::vector<String> UIStrings::LoadFromXml(const XmlNode& node) {
        eastl::vector<String> problems;
        if (!node) {
            return problems;
        }

        for (const XmlNode child : node.Children()) {
            const StringView name = child.Name();
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
                const StringView entryName = entry.Name();
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
        for (String& problem : problems) {
            problem = UIText::TidyMessage(StringView(problem));
        }
        return problems;
    }

}  // namespace BECore
