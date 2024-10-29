//
// Created by kandu on 11.10.2024.
//

#pragma once

#include <ctime>

namespace ds1307 {
    constexpr uint8_t RTC_ADDR = 0x68;
    constexpr std::time_t INVALID_TIME = -1;
    std::time_t time(std::time_t *arg);

    bool is_enabled();

    void set_oscillator(bool enable);

    void set_time(std::time_t time);
}
