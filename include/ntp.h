//
// Created by kandu on 14.10.2024.
//

#pragma once

#include <ctime>
#include <string>
#include <string_view>
#include <Arduino.h>
#include <WiFiUdp.h>

namespace ntp {
    constexpr uint16_t LISTENING_PORT = 8888;
    constexpr uint16_t NTP_PORT = 123;

    enum class CORRECTION : uint8_t {
        NO_WARN = 0,
        LM_61,
        LM_59,
        NOT_SYNC
    };

    enum class MODE: uint8_t {
        RESERVED = 0,
        SYMMETRIC_ACTIVE,
        SYMMETRIC_PASSIVE,
        CLIENT,
        SERVER,
        BROADCAST,
        RESERVED0,
        RESERVED1
    };


    class ntp_client {
    public:

        ntp_client() = default;
        ntp_client(const ntp_client&) = delete;
        ntp_client& operator=(const ntp_client&) = delete;
        ntp_client(ntp_client&&) = delete;
        ntp_client& operator=(ntp_client&&) = delete;

        ntp_client(std::string_view address, uint16_t port=NTP_PORT) {
            set_server(address, port);
        }

        ~ntp_client(){
            socket_.stop();
        }

        void set_server(std::string_view address, uint16_t port=NTP_PORT) {
            address_ = address;
            port_ = port;
            is_initialized_ = false;
            if(!socket_.begin(LISTENING_PORT)) {
                Serial.println("#NTP No UDP sockets available to use");
                return;
            }
            is_initialized_ = true;
        }

        [[nodiscard]] std::time_t get_time() const {
            return {};
        }

        [[nodiscard]] std::tm get_time_struct() const {
            return {};
        }

        [[nodiscard]] bool sync() const { // Синхронизирует время с NTP сервером, возвращает успех операции
            return false;
        }

//        void setTimeout(uint16_t timeout);  // Устанавливает время ожидания в миллисекундах
//        void setUpdateInterval(uint32_t interval);  // Устанавливает интервал между обновлениями времени
//        void setTimeZone(int offset_seconds);  // Устанавливает смещение от UTC
//        bool operator==(const NTPClient& other) const;  // Сравнение двух NTP клиентов
//        bool operator!=(const NTPClient& other) const;
//        bool isSynchronized() const;  // Проверяет, успешно ли прошла последняя синхронизация
//        void setManualTime(std::time_t manual_time);  // Позволяет вручную задать время
//        std::string formatTime(std::time_t time, const std::string& format);  // Форматирование времени в строку

    private:
        std::string address_;
        uint16_t port_{0};
        WiFiUDP socket_;
        bool is_initialized_{false};
    };
    std::time_t time(std::time_t *arg);
} // namespace ntp