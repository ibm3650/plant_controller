//
// Created by kandu on 11.10.2024.
//
#include "ds1307.h"
#include <Wire.h>
#include <Arduino.h>
//TODO: полный функцийонал ды1307
//TODO: соответствие С и с++ api interface
namespace{
    uint32_t bcd_to_dec(const uint8_t* bytes, size_t length){
        uint32_t accumulator{};
        for (size_t i = 0; i < length; ++i) {
            uint8_t tmp = (bytes[i] >> 4);
            assert(tmp <= 9);
            accumulator = accumulator * 10 + tmp;
            tmp = (bytes[i] & 0x0F);
            assert(tmp <= 9);
            accumulator = accumulator * 10 + tmp;
        }
        return accumulator;
    }
    //TODO: учет буффера i2c
    size_t read_memspace(uint8_t address, uint8_t* buffer, size_t length){
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
};


#define DEBUG(var, name)    {\
Serial.printf("DEBUG[%lu] : %s#%s#%d : %s", millis(), __FILE__, __FUNCTION__, __LINE__, name ? name : "unnamed");\
Serial.println(var);\
}

#define DEBUG_N(var)  DEBUG(var, nullptr)

enum class FORMAT: bool{H24 = true, H12 = false};
//TODO: учет остальых позиций а также изначально установленного знгачения
void set_format(FORMAT fmt){
    uint8_t hours;
    read_memspace(0x02, &hours, sizeof(hours));
    Wire.beginTransmission(ds1307::RTC_ADDR);
    Wire.write(0x02);
    if (static_cast<bool>(fmt)) {
        // Устанавливаем 24-часовой формат: сбросить бит 6
        hours &= ~(1 << 6);
    } else {
        // Устанавливаем 12-часовой формат: установить бит 6
        hours |= (1 << 6);

        // Если хотите установить AM/PM режим, измените бит 5:
        // hour_reg |= (1 << 5);  // PM
        // hour_reg &= ~(1 << 5); // AM
    }
    Wire.write(hours);
    //Wire.write((hours & ~static_cast<uint8_t>(fmt)) | static_cast<uint8_t>(fmt));
    Wire.endTransmission();
}

//TODO: учитывать 24\12 формат
//TODO: установка в 12часовом ормате треьует конвертации
std::time_t ds1307::time( std::time_t* arg ){
    constexpr uint8_t datetime_registers_len = 7;
    uint8_t buffer[datetime_registers_len] = {};

    if(::read_memspace(0x00, buffer, datetime_registers_len) != datetime_registers_len)
        return static_cast<std::time_t>(-1);

    uint8_t dbg;
    DEBUG(::bcd_to_dec(buffer, 1), "Seconds: ");
    DEBUG(::bcd_to_dec(buffer + 1, 1), "Minutes: ");
    dbg = buffer[2] & (1 << 6);
    uint8_t hours = ::bcd_to_dec(buffer + 2, 1);
    DEBUG(buffer[2], "Hours raw: ");
    if(dbg){
        DEBUG(buffer[2] & (1 << 5) ? "PM" : "AM", "In 12-hours format: ");
        hours = (buffer[2] & 0x0F) + ((buffer[2] >> 4) & 0x01) * 10;
        if (hours == 0) {
            hours = 12;  // 00h в 12-часовом формате — это 12 AM
        }
        set_format(FORMAT::H24);
    }else{
        set_format(FORMAT::H12);
    }
    DEBUG(hours, "Hours: ");
    DEBUG(::bcd_to_dec(buffer + 3, 1), "Day of week: ");
    DEBUG(::bcd_to_dec(buffer + 4, 1), "Date: ");
    DEBUG(::bcd_to_dec(buffer + 5, 1), "Month: ");
    DEBUG(::bcd_to_dec(buffer + 6, 1) + 2000, "Year: ");
    return -1;
    std::time_t accumulator{};

    std::tm date;
    uint8_t tmp = Wire.read();
    accumulator += ::bcd_to_dec(&tmp, 1);

    tmp = Wire.read();
    accumulator += ::bcd_to_dec(&tmp, 1) * 60;

    tmp = Wire.read();
    accumulator += ::bcd_to_dec(&tmp, 1) * 3600;

    tmp = Wire.read();//skip day

    tmp = Wire.read();
    accumulator += ::bcd_to_dec(&tmp, 1) * 86400;

    tmp = Wire.read();
    accumulator += ::bcd_to_dec(&tmp, 1) * 86400;
//    while(Wire.available()){
//        Serial.print("Got DS byte: ");
//        uint8_t ret = Wire.read();
//        Serial.print("{ ");
//        Serial.print((ret & 0xF0) >> 4);
//        Serial.print(", ");
//        Serial.print((ret & 0x0F));
//
//        Serial.println("}");
//    }
return {};
}


