#pragma once

namespace BECore {

    /**
     * @brief The variables that UI text is bound to: game code sets a value, a label that shows `{name}` follows it.
     *
     * `UIVars` is a table of name to text with a version that grows on every change, and nothing more: there is no expression language. Game code writes
     * `CoreManager::GetUIVars().Set("score"_intern, 120)`; a label with `text="Score: {score}"` expands its text when the version changed, so an unchanged screen
     * costs one comparison per label. A name nobody set expands to nothing; `{{` and `}}` are a literal brace. Main thread only.
     */
    class UIVars {
    public:
        /// Sets a variable. A value equal to the current one changes nothing, so the version stays and no label is laid out again
        void Set(PoolString name, StringView value);

        /// A number is written the way Format() writes it ("120", "0.5")
        template <typename T>
        requires std::is_arithmetic_v<T> void Set(PoolString name, T value) {
            const eastl::string text = Format("{}", value);
            Set(name, StringView(text));
        }

        /// The value of a variable, or an empty text when nobody set it
        [[nodiscard]] StringView Get(PoolString name) const;

        [[nodiscard]] bool Has(PoolString name) const;

        /// @return true when the variable existed
        bool Remove(PoolString name);

        /// Drops every variable (a new game, and tests between cases)
        void Clear();

        /// Grows on every change; never goes back, not even after Clear()
        [[nodiscard]] uint64_t GetVersion() const {
            return _version;
        }

        /// @p text with every `{name}` replaced by the value of the variable; `{{` and `}}` become one brace. An unclosed `{` stays as it is
        [[nodiscard]] eastl::string Expand(StringView text) const;

        /// True when Expand() could change @p text, so a label skips the work for a plain text
        [[nodiscard]] static bool HasPlaceholders(StringView text);

    private:
        UnorderedPoolMap<eastl::string> _values;
        uint64_t _version = 0;
    };

}  // namespace BECore
