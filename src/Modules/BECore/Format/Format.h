#pragma once

#include <fmt/format.h>

/**
 * @brief Специализации fmt::formatter для строк EASTL (String и StringView из BECore/Types/Strings.h).
 * Это позволяет использовать их напрямую в функциях форматирования. Строка с любым аллокатором форматируется так же.
 */
template <typename Allocator>
struct fmt::formatter<eastl::basic_string<char, Allocator>> : formatter<std::string_view> {  // std: fmt formats std::string_view
    auto format(const eastl::basic_string<char, Allocator>& s, format_context& ctx) const {
        return formatter<std::string_view>::format(BECore::ToStd(BECore::StringView{s}), ctx);  // std: fmt
    }
};

template <>
struct fmt::formatter<BECore::StringView> : formatter<std::string_view> {  // std: fmt formats std::string_view
    auto format(BECore::StringView s, format_context& ctx) const {
        return formatter<std::string_view>::format(BECore::ToStd(s), ctx);  // std: fmt
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
         * @brief Форматирует строку и возвращает String. Короткий результат не выделяет память, кроме самой строки.
         */
        template <typename... Args>
        [[nodiscard]] inline String Format(format_string<Args...> fmt, Args&&... args) {
            fmt::basic_memory_buffer<char, 256> buffer;
            fmt::format_to(std::back_inserter(buffer), fmt, std::forward<Args>(args)...);
            return String{buffer.data(), buffer.size()};
        }

        /**
         * @brief Дописывает отформатированный текст в конец существующей строки (одно место для формата в строку).
         */
        template <typename... Args>
        inline void AppendFormat(String& out, format_string<Args...> fmt, Args&&... args) {
            fmt::format_to(eastl::back_inserter(out), fmt, std::forward<Args>(args)...);
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

    }  // namespace Details::Format

    // Алиасы для удобства в пространстве имен Core
    using Details::Format::AppendFormat;
    using Details::Format::Format;
    using Details::Format::FormatTo;
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
            // fmt::runtime принимает std::string_view
            return Details::Format::Format(fmt::runtime(ToStd(_format)), std::forward<Args>(args)...);
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
