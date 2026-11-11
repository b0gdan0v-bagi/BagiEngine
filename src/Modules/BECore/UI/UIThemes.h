#pragma once

#include <BECore/UI/UIColor.h>

namespace BECore {

    class XmlNode;

    /// What a token that no theme defines is drawn in, so a typo shows in the window and never as an invisible element
    inline constexpr Color kMissingTokenColor{255, 0, 255, 255};

    /**
     * @brief The colour themes of the in-game UI: themes, tokens and their colours. `CoreManager::GetUIThemes()`.
     *
     * A theme is a table of named colours (`primary`, `panel`, `text`); a colour attribute of a UI file refers to one with `color="@primary"`. The table holds every theme, one of
     * them is current, and SetTheme() switches it at run time like SetLanguage() of UIStrings does. Find() looks a token up in the current theme, else in the default theme (a
     * theme that leaves out a token the default has shows the default's colour), else finds nothing.
     *
     * A token name is `[A-Za-z][A-Za-z0-9_-]*`, so the same name is a CSS custom property name in the HTML front-end.
     *
     * The table is pure data: no scene, no services. It is loaded from `config/UIThemes.xml` by Initialize() (a game ships the file, the engine ships none), and the game picks the
     * theme in code. Main thread only.
     *
     * ```xml
     * <UIThemes default="light">
     *   <theme id="light"><color name="primary" value="#33aa66"/></theme>
     *   <theme id="dark"><color name="primary" value="0,90,40,255"/></theme>
     * </UIThemes>
     * ```
     * `value` is any text form of `UIText::ParseColor`.
     */
    class UIThemes {
    public:
        /// Reads `config/UIThemes.xml` through ConfigManager into the table and logs what LoadFromXml() complains about. A missing config is an empty table, with no log
        void Initialize();

        /// Whether @p token is a legal token name
        [[nodiscard]] static bool IsValidToken(eastl::string_view token);

        /**
         * @brief Sets the colour of @p token in @p theme. The first theme set is the default one until SetDefaultTheme() says otherwise
         * @return false and nothing changed when @p token is not a legal name or @p theme is empty. A value equal to the current one changes nothing (and keeps the version)
         */
        bool Set(PoolString theme, eastl::string_view token, const Color& color);

        /// The theme a token falls back to. Any id: a default with no colours is a table with no fallback
        void SetDefaultTheme(PoolString theme);
        [[nodiscard]] PoolString GetDefaultTheme() const {
            return _default;
        }

        /// @return false and nothing changed when the theme has no colours. Until it is called the current theme is the default one
        bool SetTheme(PoolString theme);
        [[nodiscard]] PoolString GetTheme() const;

        /// The themes that have colours, in the order they were first set
        [[nodiscard]] const eastl::vector<PoolString>& GetThemes() const {
            return _themeIds;
        }
        [[nodiscard]] bool HasTheme(PoolString theme) const;

        /// The colour of @p token in the current theme, else in the default theme, else nothing. Records nothing
        [[nodiscard]] std::optional<Color> Find(eastl::string_view token) const;

        /**
         * @brief Writes into @p color the channels of its token: the colour of Find(), else kMissingTokenColor with the token recorded in GetMissing() and logged, once per token.
         *        A literal colour (an empty token) is left alone. The token stays, so the colour resolves again when the theme changes
         */
        void Resolve(UIColor& color);

        /// The tokens Resolve() did not find in any theme, once each, in the order they were asked for
        [[nodiscard]] const eastl::vector<PoolString>& GetMissing() const {
            return _missing;
        }

        /// Every token any theme defines, in the order they were first set: what a theme has to define to be complete
        [[nodiscard]] const eastl::vector<PoolString>& GetTokens() const {
            return _tokens;
        }
        /// Whether @p theme itself (not its fallback to the default) defines @p token
        [[nodiscard]] bool HasToken(PoolString theme, eastl::string_view token) const;

        /// Drops every theme, the default and the current theme, and the record of missing tokens. The version grows
        void Clear();

        /// Starts at 1, grows when a colour changes, the effective theme or the default theme changes, and on Clear(); never goes back. A colour that resolved at an older
        /// version resolves again; 0 is left for "never resolved"
        [[nodiscard]] uint64_t GetVersion() const {
            return _version;
        }

        /**
         * @brief Adds the themes of a `<UIThemes>` node on top of what the table holds
         * @return the problems it found, as text for the log: a theme without an id, a colour without a name, with an illegal name or without a `value`, a `value` that is not a
         *         colour, a duplicate token in one theme (the first stays), a theme with no colours, a `default` that names no theme, an unknown element. The rest is loaded. An
         *         empty node adds nothing
         */
        eastl::vector<eastl::string> LoadFromXml(const XmlNode& node);

    private:
        struct Theme {
            PoolString id;
            UnorderedPoolMap<Color> colors;
        };

        [[nodiscard]] const Theme* FindTheme(PoolString theme) const;
        [[nodiscard]] static std::optional<Color> FindColor(const Theme* theme, eastl::string_view token);

        eastl::vector<Theme> _themes;
        eastl::vector<PoolString> _themeIds;
        eastl::vector<PoolString> _tokens;
        eastl::vector<PoolString> _missing;
        PoolString _default;
        PoolString _current;
        uint64_t _version = 1;
    };

}  // namespace BECore
