//
// Created by kandu on 11.10.2024.
//
#include "ds1307.h"
#include <Wire.h>


// TODO(kandu): полный функционал ды1307
// TODO(kandu): соответствие С и с++ api interface
namespace {
    uint32_t bcd_to_dec(const uint8_t *bytes, size_t length) {
        uint32_t accumulator{};
        for (size_t i = 0; i < length; ++i) {
            uint8_t tmp = (bytes[i] >> 4U);
            assert(tmp <= 9);
            accumulator = accumulator * 10 + tmp;
            tmp = (bytes[i] & 0x0FU);
            assert(tmp <= 9);
            accumulator = accumulator * 10 + tmp;
        }
        return accumulator;
    }

    // TODO(kandu): учет буфера i2c
    size_t read_memspace(uint8_t address, uint8_t *buffer, size_t length) {
        Wire.beginTransmission(ds1307::RTC_ADDR);
        Wire.write(address);
        if (Wire.endTransmission() != 0) {
            return 0;
        }
        const size_t bytes_read = Wire.requestFrom(ds1307::RTC_ADDR, static_cast<uint8_t>(length));
        if (bytes_read == 0) {
            return 0;
        }
        for (size_t i = 0; i < bytes_read; ++i) {
            buffer[i] = Wire.read();
        }
        return bytes_read;
    }
} // local namespace





inline bool is_24h(uint8_t hours_reg) {
    return hours_reg & (1U << 6U);
}


uint8_t get_hours24(uint8_t hours_reg) {
    if (!(hours_reg & (1U << 6U))) {
        return ::bcd_to_dec(&hours_reg, 1);
    }
    const bool is_pm = hours_reg & (1U << 5U);
    hours_reg &= 0x1FU;
    hours_reg = ::bcd_to_dec(&hours_reg, 1);
    if (is_pm) {
        return hours_reg == 12 ? hours_reg : hours_reg + 12;
    }
    return hours_reg == 12 ? 0 : hours_reg;
}

static uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10 * 16) + (val % 10));
}

enum class FORMAT : bool {
    H24 = true, H12 = false
};

// TODO(kandu): учет остальных позиций а также изначально установленного значения
void set_format(FORMAT fmt) {
    uint8_t hours = 0;
    read_memspace(0x02, &hours, sizeof(hours));
    Wire.beginTransmission(ds1307::RTC_ADDR);
    Wire.write(0x02);
    if (static_cast<bool>(fmt)) {
        // Устанавливаем 24-часовой формат: сбросить бит 6
        hours = dec_to_bcd(get_hours24(hours));
        //hours &= ~(1 << 6);
    } else {
        // Устанавливаем 12-часовой формат: установить бит 6
        const uint8_t h24 = get_hours24(hours);
        if (h24 == 12 || h24 == 0)
            hours = 12;
        else if (h24 < 12)
            hours = h24;
        else
            hours = h24 - 12;
        hours = dec_to_bcd(hours);
        if (h24 >= 12)
            hours |= (1U << 5U); // PM
        else
            hours &= ~(1U << 5U); // AM
        hours |= (1U << 6U);
    }
    Wire.write(hours);
    Wire.endTransmission();
}

bool ds1307::is_enabled() {
    uint8_t control = 0;
    read_memspace(0x00, &control, sizeof(control));
    return !(control & (1U << 7U));
}

void ds1307::set_time(std::time_t time) {
    uint8_t buffer[7] = {};
    std::tm *tm = std::localtime(&time);
    buffer[0] = dec_to_bcd(tm->tm_sec);
    buffer[1] = dec_to_bcd(tm->tm_min);
    buffer[2] = dec_to_bcd(tm->tm_hour);
    buffer[3] = dec_to_bcd(tm->tm_wday);
    buffer[4] = dec_to_bcd(tm->tm_mday);
    buffer[5] = dec_to_bcd(tm->tm_mon + 1);
    buffer[6] = dec_to_bcd(tm->tm_year - 100);
    Wire.beginTransmission(ds1307::RTC_ADDR);
    Wire.write(0x00);
    Wire.write(buffer, sizeof(buffer));
    Wire.endTransmission();

}

void ds1307::set_oscillator(bool enable) {
    uint8_t control = 0;
    read_memspace(0x00, &control, sizeof(control));
    Wire.beginTransmission(ds1307::RTC_ADDR);
    Wire.write(0x00);
    if (enable) {
        control &= ~(1U << 7U);
    } else {
        control |= (1U << 7U);
    }
    Wire.write(control);
    Wire.endTransmission();
}

// TODO(kandu): учитывать 24\12 формат
// TODO(kandu): установка в 12часовом формате требует конвертации
std::time_t ds1307::time(std::time_t *arg) {
    const uint16_t days_since[] = {0, 31, 59, 90, 120, 151,
                                   181, 212, 243, 273, 304, 334};
    constexpr uint8_t datetime_registers_len = 7;
    uint8_t buffer[datetime_registers_len] = {};
    if (::read_memspace(0x00, buffer, datetime_registers_len) != datetime_registers_len) {
        return INVALID_TIME;
    }
    if (buffer[0] & (1U << 7U)) {
        return INVALID_TIME;
    }
    uint8_t const hours = get_hours24(buffer[2]);
    const uint16_t year = ::bcd_to_dec(buffer + 6, 1) + 2000;
    uint8_t const month = ::bcd_to_dec(buffer + 5, 1);

    std::time_t epoch = ((year - 1970) * 365) + ((year - 1970) / 4);
    epoch += days_since[month - 1];
    epoch += ::bcd_to_dec(buffer + 4, 1) - 1;
    epoch += (!(year % 4) && month >= 3) ? 1 : 0;
    epoch *= 86400;
    epoch += ::bcd_to_dec(buffer, 1);
    epoch += ::bcd_to_dec(buffer + 1, 1) * 60;
    epoch += hours * 60 * 60;
    return epoch;
}


