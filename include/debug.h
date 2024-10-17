//
// Created by kandu on 17.10.2024.
//

#pragma once
#include <Arduino.h>


enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

// Макрос для текущего уровня отладки
#ifdef DEBUGN
constexpr LogLevel CURRENT_LOG_LEVEL = LogLevel::DEBUG;  // Включаем отладку
#else
constexpr LogLevel CURRENT_LOG_LEVEL = LogLevel::ERROR;  // Отключаем отладку, оставляем только ошибки
#endif










// Макросы для упрощенного использования
#ifdef DEBUGN
#define LOG_DEBUG(...)   log_message(LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)    log_message(LogLevel::INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) log_message(LogLevel::WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)   log_message(LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)
#else
#define LOG_DEBUG(...)   // Не выводим ничего, если отладка отключена
    #define LOG_INFO(...)    // Не выводим ничего, если отладка отключена
    #define LOG_WARNING(...) // Не выводим ничего, если отладка отключена
    #define LOG_ERROR(...)   log_message(LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)  // Только ошибки
#endif
