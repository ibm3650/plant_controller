#pragma clang diagnostic push
#pragma ide diagnostic ignored "fuchsia-default-arguments-calls"
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-magic-numbers"
#pragma ide diagnostic ignored "modernize-use-trailing-return-type"
#pragma ide diagnostic ignored "misc-include-cleaner"
//
// Created by kandu on 09.10.2024.
//
#include "24c32.h"
#include <Wire.h>
#include <algorithm>
#include <span>

//Размер буфера шины TWI - длинна адреса (2 байта)
static constexpr auto eeprom_block_size = std::min(TWI_BUFFER_LENGTH - sizeof(uint16_t),
                                                   static_cast<size_t>(eeprom::PAGE_SIZE));

bool eeprom::write_page(uint16_t address,  std::span<const uint8_t> buffer) {
    while (!buffer.empty()) {
        //Количество байт до конца страницы
        const size_t bytes_in_page = PAGE_SIZE - (address % PAGE_SIZE);
        //Выбор количества байт для фактического считывания, меньше или равных количеству байт в буфере шины
        //Потому как, если количество байт для считывания больше, чем размер буфера шины, то оно не считается
        //Также страничное чтение быстрее, чем последовательное
        const size_t bytes_count = std::min({buffer.size(), eeprom_block_size, bytes_in_page});
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write(static_cast<uint8_t>(address >> 8U)); // MSB адреса
        Wire.write(static_cast<uint8_t>(address & 0xFFU)); // LSB адреса
        Wire.write(buffer.data(), bytes_count);
        if (Wire.endTransmission() != 0) {
            return false;
        }
        //Задержка для записи данных в память микросхемы, по даташиту необходимо 10 мс
        delay(WRITE_DELAY_MS);
        buffer = buffer.subspan(bytes_count);
        address += bytes_count;
    }
    return true;
}


bool eeprom::read_random(uint16_t address, std::span<uint8_t> buffer) {
    while (!buffer.empty()) {
        //Количество байт до конца страницы
        const size_t bytes_in_page = PAGE_SIZE - (address % PAGE_SIZE);
        //Выбор количества байт для фактического считывания, меньше или равных количеству байт в буфере шины
        //Потому как, если количество байт для считывания больше, чем размер буфера шины, то оно не считается
        //Также страничное чтение быстрее, чем последовательное
        const size_t bytes_count = std::min({buffer.size(), eeprom_block_size, bytes_in_page});
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write(static_cast<uint8_t>(address >> 8U)); // MSB адреса
        Wire.write(static_cast<uint8_t>(address & 0xFFU)); // LSB адреса
        if (Wire.endTransmission() != 0) {
            return false;
        }

        const size_t bytes_read = Wire.requestFrom(static_cast<int>(EEPROM_ADDR),
                                                   static_cast<int>(bytes_count));
        if (bytes_read != bytes_count) {
            return false;
        }
        for (size_t i = 0; i < bytes_read; ++i) {
            buffer[i] = Wire.read();
        }

        buffer = buffer.subspan(bytes_read);
        address += bytes_read;
    }
    return true;
}

#pragma clang diagnostic pop