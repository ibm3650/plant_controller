//
// Created by kandu on 09.10.2024.
//

#pragma once
#include <Arduino.h>
#include <Wire.h>
#define EEPROM_ADDR  0x50
#define PAGE_SIZE   32
constexpr auto eeprom_block_size = std::min(TWI_BUFFER_LENGTH - sizeof(uint16_t),
                                            static_cast<size_t>(PAGE_SIZE));

void write_page(uint16_t address, const uint8_t* data, size_t length);
void read_random(uint16_t address, uint8_t* buffer, size_t length);
