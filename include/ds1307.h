//
// Created by kandu on 11.10.2024.
//

#pragma once

#include <ctime>

#define TIMEZONE    3

namespace ds1307 {
    constexpr uint8_t RTC_ADDR = 0x68;

    std::time_t time(std::time_t *arg);

    bool is_enabled();

    void set_oscilator(bool enable);

    void set_time(std::time_t time);
} // namespace ds1307;
