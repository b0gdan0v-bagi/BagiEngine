#include "LogEvent.h"

namespace BECore {

    bool& LogEvent::GetBufferingFlag() {
        static bool buffering = false;  // По умолчанию буферизация включена
        return buffering;
    }

    eastl::vector<LogEvent::BufferedLogEntry>& LogEvent::GetBuffer() {
        static eastl::vector<BufferedLogEntry> buffer;
        return buffer;
    }

    void LogEvent::EnableBuffering() {
        GetBufferingFlag() = true;
    }

    void LogEvent::DisableBuffering() {
        GetBufferingFlag() = false;
    }

    bool LogEvent::IsBuffering() {
        return GetBufferingFlag();
    }

    void LogEvent::Emit(LogLevel level, eastl::string_view message) {
        if (GetBufferingFlag()) {
            // Буферизуем лог
            GetBuffer().push_back({level, eastl::string(message)});
            return;
        }

        // Отправляем через базовый Emit
        EventBase<LogEvent>::Emit(level, message);
    }

    void LogEvent::FlushBuffer() {
        auto& buffer = GetBuffer();
        auto& buffering = GetBufferingFlag();

        // Отключаем буферизацию перед проигрыванием
        buffering = false;

        // Проигрываем все буферизованные логи
        for (const auto& entry : buffer) {
            Emit(entry.level, entry.message);
        }

        // Очищаем буфер
        buffer.clear();
    }

}  // namespace BECore
