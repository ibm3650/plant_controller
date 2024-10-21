//
// Created by kandu on 09.10.2024.
//

#pragma once
#include <Arduino.h>

namespace eeprom{
constexpr uint8_t EEPROM_ADDR = 0x50;
constexpr uint8_t PAGE_SIZE = 32;
constexpr uint16_t STORAGE_SIZE = 32 / 8 * 1024;


void write_page(uint16_t address, const uint8_t *data, size_t length);

void read_random(uint16_t address, uint8_t *buffer, size_t length);

}; // namespace eeprom