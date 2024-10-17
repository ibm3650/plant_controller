//
// Created by kandu on 14.10.2024.
//

#pragma once

#include <ctime>
#include <string>
#include <string_view>
#include <Arduino.h>
#include <WiFiUdp.h>
#include "async_wait.h"

namespace ntp {
    constexpr uint16_t LISTENING_PORT = 8888;
    constexpr uint16_t NTP_PORT = 123;
    constexpr uint16_t CONNECTION_TIMEOUT = 1000;//ms
    constexpr uint16_t UPDATE_INTERVAL = 60;//s

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

    enum class UPDATE_STATUS : uint8_t {
        NO_UPDATE = 0,
        UPDATE_SUCCESS,
        UPDATE_FAILED
    };

    struct sntp_msg_t {
        uint8_t correction : 2;
        uint8_t version : 3;
        uint8_t mode : 3;
        uint8_t stratum;
        uint8_t poll;
        int8_t precision;
        int32_t root_delay;
        uint32_t root_dispersion;
        uint32_t reference_id;
        uint64_t reference_timestamp;
        uint64_t originate_timestamp;
        uint64_t receive_timestamp;
        uint64_t transmit_timestamp;
    } __attribute__((packed));

    class ntp_client {
    public:
        ntp_client() = default;

        explicit ntp_client(std::string_view address, uint16_t port=NTP_PORT) noexcept;

        ntp_client(const ntp_client&) = default;

        ntp_client& operator=(const ntp_client&) = default;

        ntp_client(ntp_client&&) = default;

        ntp_client& operator=(ntp_client&&) = default;

        ~ntp_client() = default;

        void set_server(std::string_view address, uint16_t port=NTP_PORT) noexcept;

        [[nodiscard]] std::optional<std::time_t> get_time() const noexcept;

        [[nodiscard]] std::optional<std::tm> get_time_struct() const noexcept;

        //Polls the NTP server and returns the correction value
        [[nodiscard]] UPDATE_STATUS sync_poll() noexcept;

        [[nodiscard]] bool sync() noexcept;

        [[nodiscard]] bool is_synchronized() const noexcept;

        void set_timezone_offset(uint8_t offset) noexcept;

        void set_timeout(size_t timeout) noexcept;

        void set_update_interval(size_t interval) noexcept;
    private:
        //FIXME: Учитывать потокобезопасность. Обновлять и получать можно одновременно,использовать таймер задержки. избегать гонок
        sntp_msg_t message_{};
        async_wait delay_timer_{0};
        size_t connection_timeout_{CONNECTION_TIMEOUT};
        size_t update_interval_{UPDATE_INTERVAL};
        size_t last_update_{0};
        std::string address_;
        uint16_t port_{0};
        WiFiUDP socket_;

        bool is_initialized_{false};
        uint8_t time_zone_offset_ = 0;
    };
    std::time_t time(std::time_t *arg);
} // namespace ntp