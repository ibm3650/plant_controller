//
// Created by kandu on 09.10.2024.
//
#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-magic-numbers"
#pragma ide diagnostic ignored "cppcoreguidelines-pro-bounds-pointer-arithmetic"
#pragma ide diagnostic ignored "modernize-use-trailing-return-type"
#pragma ide diagnostic ignored "misc-include-cleaner"
#pragma once

#include <Arduino.h>
#include <span>

namespace eeprom {
    constexpr uint8_t EEPROM_ADDR = 0x50;
    constexpr uint8_t PAGE_SIZE = 32;
    constexpr uint16_t STORAGE_SIZE = 32 / 8 * 1024;
    constexpr uint8_t WRITE_DELAY_MS = 10;


    [[nodiscard]] bool write_page(uint16_t address, std::span<const uint8_t> buffer);

    [[nodiscard]] bool read_random(uint16_t address, std::span<uint8_t> buffer);

} // namespace eeprom

#pragma clang diagnostic pop