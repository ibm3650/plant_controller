//
// Created by kandu on 04.10.2024.
//

#pragma once

#include <Arduino.h>

/**
 * @class async_wait
 * @brief Класс для асинхронного ожидания на основе не блокирующего таймера.
 *
 * Этот класс позволяет устанавливать таймер с задержкой и проверять,
 * закончилась ли задержка в не блокирующем режиме. Может использоваться
 * в проектах на базе Arduino или других микроконтроллерах.
 */
class async_wait {
public:
    /**
     * @brief Конструктор по умолчанию.
     *
     * Инициализирует объект без активной задержки (таймер на 0 мс).
     */
    async_wait() = default;

    /**
     * @brief Конструктор с параметром.
     *
     * Инициализирует объект с указанной задержкой в миллисекундах.
     *
     * @param milliseconds Длительность таймера в миллисекундах.
     */
    explicit async_wait(unsigned long milliseconds) {
        reload(milliseconds);
    }

    /**
     * @brief Оператор приведения к bool.
     *
     * Проверяет, активен ли таймер. Возвращает true, если таймер еще не завершен,
     * и false, если таймер завершен.
     *
     * @return true если таймер еще работает, false если завершен.
     */
    explicit operator bool() const {
        return !has_elapsed();
    }

    /**
     * @brief Перегруженный оператор вызова.
     *
     * Позволяет установить новую задержку в миллисекундах и перезапустить таймер.
     *
     * @param milliseconds Новая длительность таймера в миллисекундах.
     */
    void operator()(unsigned long milliseconds) {
        reload(milliseconds);
    }

    /**
     * @brief Проверяет, завершен ли таймер.
     *
     * Возвращает true, если таймер завершен, и false, если задержка еще продолжается.
     *
     * @return true если задержка завершена, false если нет.
     */
    [[nodiscard]] bool has_elapsed() const {
        if (_ms == 0)
            return true;
        return (millis() - _begin_time) >= _ms;
    }

    /**
     * @brief Перезапускает таймер с новой или прежней задержкой.
     *
     * Если передано значение `milliseconds`, устанавливается новая задержка.
     * В противном случае таймер перезапускается с последним установленным значением.
     *
     * @param milliseconds Новая длительность таймера в миллисекундах. Если передано 0,
     * используется предыдущее значение.
     */
    void reload(unsigned long milliseconds = 0) {
        if (milliseconds > 0) {
            _ms = milliseconds;
        }
        _begin_time = millis();
    }

private:
    unsigned long _ms = 0;          ///< Длительность таймера в миллисекундах.
    unsigned long _begin_time = 0;  ///< Время начала отсчета таймера в миллисекундах.
};
