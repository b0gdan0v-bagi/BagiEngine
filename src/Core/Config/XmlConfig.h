#pragma once

#include <Core/Math/Color.h>

namespace Core {

    /**
     * Класс для работы с XML конфигурационными файлами
     * Использует Boost.PropertyTree для парсинга XML
     */
    class XmlConfig {
    public:
        XmlConfig() = default;
        ~XmlConfig() = default;

        /**
         * Загрузить конфиг из файла
         * @param filepath Путь к XML файлу (может быть виртуальным путем через FileSystem)
         * @return true если загрузка успешна, false в противном случае
         */
        bool LoadFromFile(const std::filesystem::path& filepath);
        
        /**
         * Загрузить конфиг из файла по виртуальному пути
         * @param virtualPath Виртуальный путь (например, "config/example_config.xml")
         * @return true если загрузка успешна, false в противном случае
         */
        bool LoadFromVirtualPath(std::string_view virtualPath);

        /**
         * Загрузить конфиг из строки
         * @param xmlContent Содержимое XML в виде строки
         * @return true если загрузка успешна, false в противном случае
         */
        bool LoadFromString(const std::string& xmlContent);

        /**
         * Сохранить конфиг в файл
         * @param filepath Путь к файлу для сохранения
         * @return true если сохранение успешно, false в противном случае
         */
        bool SaveToFile(const std::filesystem::path& filepath) const;

        /**
         * Получить значение по пути (например, "config.window.width")
         * @param path Путь к значению в XML (разделитель - точка)
         * @param defaultValue Значение по умолчанию, если путь не найден
         * @return Значение или defaultValue
         */
        template<typename T>
        T Get(const std::string& path, const T& defaultValue = T{}) const {
            if constexpr (std::is_same_v<T, Math::Color>) {
                // Специализация для Math::Color
                std::string colorStr = _ptree.get<std::string>(path, "");
                if (colorStr.empty()) {
                    return defaultValue;
                }
                return Math::Color::ParseColorFromString(colorStr, defaultValue);
            } else if constexpr (std::is_enum_v<T>) {
                // Для enum используем MagicEnum для парсинга из строки
                std::string strValue = _ptree.get<std::string>(path, "");
                if (strValue.empty()) {
                    return defaultValue;
                }
                auto enumValue = magic_enum::enum_cast<T>(strValue);
                return enumValue.value_or(defaultValue);
            } else {
                return _ptree.get<T>(path, defaultValue);
            }
        }

        /**
         * Получить значение по пути (опциональное)
         * @param path Путь к значению в XML
         * @return boost::optional с значением или пустое, если не найдено
         */
        template<typename T>
        boost::optional<T> GetOptional(const std::string& path) const {
            return _ptree.get_optional<T>(path);
        }

        /**
         * Установить значение по пути
         * @param path Путь к значению
         * @param value Значение для установки
         */
        template<typename T>
        void Set(const std::string& path, const T& value) {
            _ptree.put(path, value);
        }

        /**
         * Получить дочерний узел по пути
         * @param path Путь к узлу
         * @return boost::optional с ptree или пустое, если не найдено
         */
        boost::optional<const boost::property_tree::ptree&> GetChild(const std::string& path) const {
            return _ptree.get_child_optional(path);
        }

        /**
         * Получить дочерний узел по пути (неконстантная версия)
         */
        boost::optional<boost::property_tree::ptree&> GetChild(const std::string& path) {
            return _ptree.get_child_optional(path);
        }

        /**
         * Проверить существование пути
         * @param path Путь для проверки
         * @return true если путь существует
         */
        bool Has(const std::string& path) const {
            return _ptree.find(path) != _ptree.not_found();
        }

        /**
         * Получить прямой доступ к property_tree (для продвинутого использования)
         */
        const boost::property_tree::ptree& GetTree() const { return _ptree; }
        boost::property_tree::ptree& GetTree() { return _ptree; }

        /**
         * Очистить конфиг
         */
        void Clear() {
            _ptree.clear();
        }

        /**
         * Проверить, загружен ли конфиг
         */
        bool IsLoaded() const {
            return !_ptree.empty();
        }


    private:
        boost::property_tree::ptree _ptree;
    };

}  // namespace Core

