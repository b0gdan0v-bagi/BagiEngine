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

namespace Core {

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

} // namespace Core
