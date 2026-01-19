#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <fmt/format.h>
#include <string_view>

/**
 * @brief Специализации fmt::formatter для типов EASTL.
 * Это позволяет использовать eastl::string и eastl::string_view напрямую в функциях форматирования.
 */
template <>
struct fmt::formatter<eastl::string> : formatter<std::string_view> {
    auto format(const eastl::string& s, format_context& ctx) const {
        return formatter<std::string_view>::format(std::string_view(s.data(), s.size()), ctx);
    }
};

template <>
struct fmt::formatter<eastl::string_view> : formatter<std::string_view> {
    auto format(eastl::string_view s, format_context& ctx) const {
        return formatter<std::string_view>::format(std::string_view(s.data(), s.size()), ctx);
    }
};

namespace BECore {

    /**
     * @brief Обертки над функциями форматирования для независимости от конкретной библиотеки.
     * На данный момент используется libfmt.
     */
    namespace Details::Format {

        template <typename... Args>
        using format_string = fmt::format_string<Args...>;

        /**
         * @brief Форматирует строку и возвращает eastl::string.
         */
        template <typename... Args>
        [[nodiscard]] inline eastl::string Format(format_string<Args...> fmt, Args&&... args) {
            std::string result = fmt::format(fmt, std::forward<Args>(args)...);
            return eastl::string(result.c_str(), result.size());
        }

        /**
         * @brief Печатает отформатированную строку в стандартный вывод.
         */
        template <typename... Args>
        inline void Print(format_string<Args...> fmt, Args&&... args) {
            fmt::print(fmt, std::forward<Args>(args)...);
        }

        /**
         * @brief Печатает отформатированную строку с переносом строки в стандартный вывод.
         */
        template <typename... Args>
        inline void PrintLine(format_string<Args...> fmt, Args&&... args) {
            fmt::print(fmt, std::forward<Args>(args)...);
            fmt::print("\n");
        }

        /**
         * @brief Форматирует строку в итератор вывода.
         */
        template <typename OutputIt, typename... Args>
        inline OutputIt FormatTo(OutputIt out, format_string<Args...> fmt, Args&&... args) {
            return fmt::format_to(out, fmt, std::forward<Args>(args)...);
        }

    } // namespace Details::Format

    // Алиасы для удобства в пространстве имен Core
    using Details::Format::Format;
    using Details::Format::Print;
    using Details::Format::PrintLine;
    using Details::Format::FormatTo;

    /**
     * @brief Вспомогательный класс для поддержки user-defined literal _format.
     * Позволяет использовать синтаксис: "{}"_format(value)
     */
    class FormatLiteral {
    public:
        constexpr FormatLiteral(eastl::string_view format) : _format(format) {}

        /**
         * @brief Оператор вызова для форматирования с аргументами.
         */
        template <typename... Args>
        [[nodiscard]] eastl::string operator()(Args&&... args) const {
            // Конвертируем eastl::string_view в std::string_view для fmt::runtime
            std::string_view fmtView(_format.data(), _format.size());
            return Details::Format::Format(fmt::runtime(fmtView), std::forward<Args>(args)...);
        }

    private:
        eastl::string_view _format;
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
            return FormatLiteral(eastl::string_view(str, len));
        }
    } // namespace Literals

} // namespace BECore
