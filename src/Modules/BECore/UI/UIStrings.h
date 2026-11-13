#pragma once

namespace BECore {

    class XmlNode;

    /**
     * @brief The string table of the in-game UI: languages, keys and texts. `CoreManager::GetUIStrings()`.
     *
     * A text refers to a string with `{#key}`: `text="{#menu.start}"`, or mixed with variables, `"{#score.title}: {score}"`. Resolve() replaces every `{#key}` by the string of
     * the current language, else of the default language, else by the key itself (a missing translation shows in the window and is recorded in GetMissing(), never an empty
     * label). The result then goes through `UIVars::Expand`, so a translation may contain `{score}`; a string is not resolved again (no recursion). `{{` and `}}` are left
     * alone here: they are a literal brace for the variable step.
     *
     * The table is pure data: no scene, no services, no log. It is loaded from `config/UIStrings.xml` by Initialize() (a game ships the file, the engine ships none), and the
     * game picks the language in code with SetLanguage(). Main thread only.
     *
     * ```xml
     * <UIStrings default="en">
     *   <language id="en"><string key="menu.start" text="Start"/></language>
     *   <language id="ru"><string key="menu.start" text="Начать"/></language>
     * </UIStrings>
     * ```
     */
    class UIStrings {
    public:
        /// A key that was asked for in a language and found in neither it nor the default language
        struct MissingString {
            PoolString language;
            eastl::string key;
        };

        /// Reads `config/UIStrings.xml` through ConfigManager into the table and logs what LoadFromXml() complains about. A missing config is an empty table, with no log
        void Initialize();

        /**
         * @brief Sets the text of @p key in @p language. The first language set is the default one until SetDefaultLanguage() says otherwise
         * @return false and nothing changed when @p key is empty or contains `}`, or @p language is empty. A value equal to the current one changes nothing (and keeps the version)
         */
        bool Set(PoolString language, eastl::string_view key, eastl::string_view text);

        /// The language a key falls back to. Any id: a default with no strings is a table with no fallback
        void SetDefaultLanguage(PoolString language);
        [[nodiscard]] PoolString GetDefaultLanguage() const {
            return _default;
        }

        /// @return false and nothing changed when the language has no strings. Until it is called the current language is the default one
        bool SetLanguage(PoolString language);
        [[nodiscard]] PoolString GetLanguage() const;

        /// The languages that have strings, in the order they were first set
        [[nodiscard]] const eastl::vector<PoolString>& GetLanguages() const {
            return _languageIds;
        }
        [[nodiscard]] bool HasLanguage(PoolString language) const;

        /// The text of @p key in the current language, else in the default language, else nothing. Does not record a miss
        [[nodiscard]] std::optional<eastl::string_view> Find(eastl::string_view key) const;

        /// @p text with every `{#key}` replaced (see the class comment). A missing key becomes the key text and is recorded once per language and key. An unclosed `{#`, an
        /// empty `{#}` and a `{{#` stay as written
        [[nodiscard]] eastl::string Resolve(eastl::string_view text);

        /// True when Resolve() could change @p text: a `{#` that is not part of `{{`
        [[nodiscard]] static bool HasReferences(eastl::string_view text);

        /// The keys Resolve() did not find, once per language and key, in the order they were asked for
        [[nodiscard]] const eastl::vector<MissingString>& GetMissing() const {
            return _missing;
        }

        /// Drops every language, the default and the current language, and the record of missing keys. The version grows
        void Clear();

        /// Grows when a string changes, the effective language or the default language changes, and on Clear(); never goes back. A label that resolved its text at an older
        /// version resolves it again
        [[nodiscard]] uint64_t GetVersion() const {
            return _version;
        }

        /**
         * @brief Adds the languages of a `<UIStrings>` node on top of what the table holds
         * @return the problems it found, as text for the log: a language without an id, a string without a key, with a key that has a `}`, or without a `text`, a duplicate key in
         *         one language (the first stays), a language with no strings, a `default` that names no language, an unknown element. The rest is loaded. Each problem is one line of at most
         * `UIText::kMaxMessageBytes` (it repeats text of the file, which is cut). An empty node adds nothing
         */
        eastl::vector<eastl::string> LoadFromXml(const XmlNode& node);

    private:
        struct Language {
            PoolString id;
            UnorderedPoolMap<eastl::string> strings;
        };

        [[nodiscard]] const Language* FindLanguage(PoolString language) const;
        [[nodiscard]] const eastl::string* FindString(const Language* language, eastl::string_view key) const;
        void RecordMissing(eastl::string_view key);

        eastl::vector<Language> _languages;
        eastl::vector<PoolString> _languageIds;
        eastl::vector<MissingString> _missing;
        PoolString _default;
        PoolString _current;
        uint64_t _version = 0;
    };

}  // namespace BECore
