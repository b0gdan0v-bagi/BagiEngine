#pragma once

#include <BECore/Format/Formatter.h>
#include <cstdio>
#include <fmt/format.h>

/**
 * @brief Специализации fmt::formatter для строк EASTL (String и StringView из BECore/Types/Strings.h).
 * Это позволяет использовать их напрямую в функциях форматирования. Строка с любым аллокатором форматируется так же.
 */
template <typename Allocator>
struct fmt::formatter<eastl::basic_string<char, Allocator>> : formatter<std::string_view> {  //-NoStd: fmt formats std::string_view
    auto format(const eastl::basic_string<char, Allocator>& s, format_context& ctx) const {
        return formatter<std::string_view>::format(BECore::ToStd(BECore::StringView{s}), ctx);  //-NoStd: fmt
    }
};

template <>
struct fmt::formatter<BECore::StringView> : formatter<std::string_view> {  //-NoStd: fmt formats std::string_view
    auto format(BECore::StringView s, format_context& ctx) const {
        return formatter<std::string_view>::format(BECore::ToStd(s), ctx);  //-NoStd: fmt
    }
};

namespace BECore {

    /// A format string checked at compile time against the argument types (a literal); a runtime string goes through RuntimeFormat.
    template <typename... Args>
    using FormatString = fmt::format_string<Args...>;

    /// True when a value of the type can be an argument of Format: a built-in type, a String, or a type with a Formatter<T>.
    template <typename T>
    concept Formattable = fmt::is_formattable<T>::value;

    /// What FormatToN wrote: the length the whole text has (it may exceed the buffer) and whether the buffer was too small for it.
    struct FormatResult {
        size_t size = 0;
        bool truncated = false;
    };

    /**
     * @brief A named argument for `{name}` in a format string. It holds a reference to the value, so use it only in the
     * expression of the call: `Format("{a}-{a}", FormatArg("a", 1))`, never store the result.
     */
    template <typename T>
    [[nodiscard]] auto FormatArg(const char* name, const T& value) {
        return fmt::arg(name, value);
    }

    /// Marks a format string that is not a literal, so it is parsed at run time (a bad string is an error at run time, not at compile time).
    [[nodiscard]] inline auto RuntimeFormat(StringView text) {
        return fmt::runtime(ToStd(text));
    }

    /**
     * @brief Обертки над функциями форматирования для независимости от конкретной библиотеки.
     * На данный момент используется libfmt.
     */
    namespace Details::Format {

        /**
         * @brief Formats a string and returns a String. A short result allocates nothing but the string itself.
         */
        template <typename... Args>
        [[nodiscard]] inline String Format(FormatString<Args...> fmt, Args&&... args) {
            fmt::basic_memory_buffer<char, 256> buffer;
            fmt::format_to(std::back_inserter(buffer), fmt, std::forward<Args>(args)...);
            return String{buffer.data(), buffer.size()};
        }

        /**
         * @brief Appends the formatted text to the end of an existing string (one place for formatting into a string).
         */
        template <typename... Args>
        inline void AppendFormat(String& out, FormatString<Args...> fmt, Args&&... args) {
            fmt::format_to(eastl::back_inserter(out), fmt, std::forward<Args>(args)...);
        }

        /**
         * @brief Печатает отформатированную строку в стандартный вывод.
         */
        template <typename... Args>
        inline void Print(FormatString<Args...> fmt, Args&&... args) {
            fmt::print(fmt, std::forward<Args>(args)...);
        }

        /**
         * @brief Prints the formatted text into a C stream.
         */
        template <typename... Args>
        inline void Print(std::FILE* stream, FormatString<Args...> fmt, Args&&... args) {
            fmt::print(stream, fmt, std::forward<Args>(args)...);
        }

        /**
         * @brief Печатает отформатированную строку с переносом строки в стандартный вывод.
         */
        template <typename... Args>
        inline void PrintLine(FormatString<Args...> fmt, Args&&... args) {
            fmt::print(fmt, std::forward<Args>(args)...);
            fmt::print("\n");
        }

        /**
         * @brief Форматирует строку в итератор вывода.
         */
        template <typename OutputIt, typename... Args>
        inline OutputIt FormatTo(OutputIt out, FormatString<Args...> fmt, Args&&... args) {
            return fmt::format_to(out, fmt, std::forward<Args>(args)...);
        }

        /**
         * @brief Formats into a buffer of `capacity` characters and allocates nothing. The text is cut at the capacity, the
         * buffer is not zero-terminated, and the result tells the full length.
         */
        template <typename... Args>
        inline FormatResult FormatToN(char* buffer, size_t capacity, FormatString<Args...> fmt, Args&&... args) {
            const auto result = fmt::format_to_n(buffer, capacity, fmt, std::forward<Args>(args)...);
            const auto size = static_cast<size_t>(result.size);
            return FormatResult{size, size > capacity};
        }

    }  // namespace Details::Format

    // Алиасы для удобства в пространстве имен Core
    using Details::Format::AppendFormat;
    using Details::Format::Format;
    using Details::Format::FormatTo;
    using Details::Format::FormatToN;
    using Details::Format::Print;
    using Details::Format::PrintLine;

    /**
     * @brief Вспомогательный класс для поддержки user-defined literal _format.
     * Позволяет использовать синтаксис: "{}"_format(value)
     */
    class FormatLiteral {
    public:
        constexpr FormatLiteral(StringView format) : _format(format) {}

        /**
         * @brief Оператор вызова для форматирования с аргументами.
         */
        template <typename... Args>
        [[nodiscard]] String operator()(Args&&... args) const {
            return Details::Format::Format(RuntimeFormat(_format), std::forward<Args>(args)...);
        }

    private:
        StringView _format;
    };

    /**
     * @brief User-defined literal для форматирования строк.
     * Использование: "{}"_format(value)
     *
     * @example
     * auto str = "Hello, {}!"_format("World");
     * auto num = "Value: {}"_format(42);
     */
    inline namespace Literals {
        [[nodiscard]] constexpr FormatLiteral operator""_format(const char* str, size_t len) {
            return FormatLiteral(StringView(str, len));
        }
    }  // namespace Literals

}  // namespace BECore
