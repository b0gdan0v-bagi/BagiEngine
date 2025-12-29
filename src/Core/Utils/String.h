#pragma once

namespace Core {

    /**
     * Разделяет строку на вектор строк по указанному разделителю
     * @param str Строка для разделения
     * @param delimiter Разделитель (символ)
     * @return Вектор строк, полученных после разделения
     */
    std::vector<std::string> Split(const std::string& str, char delimiter);

}  // namespace Core

