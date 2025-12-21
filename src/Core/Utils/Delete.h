#pragma once

namespace Core {
    /**
     * Удаляет объект через delete
     * @tparam T Тип объекта
     * @param ptr Указатель на объект для удаления
     */
    template <typename T>
    void Delete(T* ptr) {
        delete ptr;
    }
}

