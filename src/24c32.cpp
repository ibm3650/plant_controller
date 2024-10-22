//
// Created by kandu on 09.10.2024.
//
#include "24c32.h"
#include <Wire.h>
#include <algorithm>

constexpr auto eeprom_block_size = std::min(TWI_BUFFER_LENGTH - sizeof(uint16_t),
                                            static_cast<size_t>(eeprom::PAGE_SIZE));

void eeprom::write_page(uint16_t address, const uint8_t *data, size_t length) {
    while (length > 0) {
        const size_t bytes_in_page = PAGE_SIZE - address % PAGE_SIZE;
        const size_t bytes_count = std::min({length, eeprom_block_size, bytes_in_page});
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write(address >> 8);
        Wire.write(address & 0xFF);
        Wire.write(data, bytes_count);
        if (Wire.endTransmission() != 0) {
            return;
        }
        delay(10);
        length -= bytes_count;
        data += bytes_count;
        address += bytes_count;
    }
}


void eeprom::read_random(uint16_t address, uint8_t *buffer, size_t length) {
    while (length > 0) {
        const size_t bytes_in_page = PAGE_SIZE - address % PAGE_SIZE;
        const int bytes_count = std::min({length, eeprom_block_size, bytes_in_page});
        Wire.beginTransmission(eeprom::EEPROM_ADDR);
        Wire.write(address >> 8);
        Wire.write(address & 0xFF);
        if (Wire.endTransmission() != 0) {
            return;
        }

        const size_t bytes_read = Wire.requestFrom(static_cast<int>(eeprom::EEPROM_ADDR), bytes_count);
        if (bytes_read == 0) {
            return;
        }

        for (size_t i = 0; i < bytes_read; ++i) {
            buffer[i] = Wire.read();
        }

        length -= bytes_read;
        buffer += bytes_read;
        address += bytes_read;
    }
}