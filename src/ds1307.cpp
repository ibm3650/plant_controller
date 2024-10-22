//
// Created by kandu on 11.10.2024.
//
#include "ds1307.h"
#include <Arduino.h>
#include <Wire.h>
#define DEBUG(var, name)    {\
Serial.printf("DEBUG[%lu] : %s#%s#%d : %s", millis(), __FILE__, __FUNCTION__, __LINE__, name ? name : "unnamed");\
Serial.println(var);\
}

#define DEBUG_N(var)  DEBUG(var, nullptr)

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
//        while(Wire.available()) {
//           Wire.flush();
//        }
        Wire.beginTransmission(ds1307::RTC_ADDR);
        Wire.write(address);
        if (Wire.endTransmission() != 0) {
            return 0;
        }
        const size_t bytes_read = Wire.requestFrom(ds1307::RTC_ADDR, static_cast<uint8_t>(length));
        //DEBUG(bytes_read, "Bytes read: ");
        if (bytes_read == 0) {
            return 0;
        }
        for (size_t i = 0; i < bytes_read; ++i) {
            buffer[i] = Wire.read();
        }
        return bytes_read;
    }
} // local namespace





inline bool is_24h(uint8_t hours_reg){
    return hours_reg & (1 << 6);
}


uint8_t get_hours24(uint8_t hours_reg) {
    if (!(hours_reg & (1 << 6))) {
        return ::bcd_to_dec(&hours_reg, 1);
    }
    const bool is_pm = hours_reg & (1 << 5);
    hours_reg &= 0x1F;
    hours_reg = ::bcd_to_dec(&hours_reg, 1);
    if (is_pm) {
        return hours_reg == 12 ? hours_reg : hours_reg + 12;
    }
    return hours_reg == 12 ? 0 : hours_reg;
}

static uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10 * 16) + (val % 10));
}
enum class FORMAT: bool{H24 = true, H12 = false};
//TODO: учет остальых позиций а также изначально установленного знгачения
void set_format(FORMAT fmt){
    uint8_t hours;
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
        if(h24 == 12 || h24 == 0)
            hours = 12;
        else if(h24 < 12)
            hours = h24;
        else
            hours = h24 - 12;
        hours = dec_to_bcd(hours);
        if(h24 >= 12)
            hours |= (1 << 5); // PM
        else
            hours &= ~(1 << 5); // AM
        hours |= (1 << 6);
        // Если хотите установить AM/PM режим, измените бит 5:
        // hour_reg |= (1 << 5);  // PM
        // hour_reg &= ~(1 << 5); // AM
    }
    Wire.write(hours);
    //Wire.write((hours & ~static_cast<uint8_t>(fmt)) | static_cast<uint8_t>(fmt));
    Wire.endTransmission();
}

bool ds1307::is_enabled() {
    uint8_t control = 0;
    read_memspace(0x00, &control, sizeof(control));
    return !(control & (1 << 7));
}

void ds1307::set_time(std::time_t time){
    uint8_t buffer[7] = {};
    std::tm* tm = std::localtime(&time);
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

void ds1307::set_oscilator(bool enable){
    uint8_t control = 0;
    read_memspace(0x00, &control, sizeof(control));
    Wire.beginTransmission(ds1307::RTC_ADDR);
    Wire.write(0x00);
    if(enable)
        control &= ~(1 << 7);
    else
        control |= (1 << 7);
    Wire.write(control);
    Wire.endTransmission();
}

//TODO: учитывать 24\12 формат
//TODO: установка в 12часовом ормате треьует конвертации
std::time_t ds1307::time( std::time_t* arg ){
    constexpr uint8_t datetime_registers_len = 7;
    uint8_t buffer[datetime_registers_len] = {};
    //Serial.println("Get time");
    //delay(10);
    if(::read_memspace(0x00, buffer, datetime_registers_len) != datetime_registers_len) {
        //Serial.println("Error reading from RTC");
        //return static_cast<std::time_t>(1);
        return static_cast<std::time_t>(-1);
    }
    if(buffer[0] & (1 << 7))
        return static_cast<std::time_t>(-1);
    //DEBUG(::bcd_to_dec(buffer, 1), "Seconds: ");
    //DEBUG(::bcd_to_dec(buffer + 1, 1), "Minutes: ");

    //set_format(FORMAT::H24);

    uint8_t const hours = get_hours24(buffer[2]);
    const uint16_t year = ::bcd_to_dec(buffer + 6, 1) + 2000;
    uint8_t const month  =::bcd_to_dec(buffer + 5, 1);
   // DEBUG(hours, "Hours: ");
   // DEBUG(::bcd_to_dec(buffer + 3, 1), "Day of week: ");
  //  DEBUG(::bcd_to_dec(buffer + 4, 1), "Date: ");
   // DEBUG(month, "Month: ");
  //  DEBUG(year, "Year: ");
    const int Days_Since[] = {0, 31, 59, 90, 120, 151,
                        181, 212, 243, 273, 304, 334};

    std::time_t years_ue = (year - 1970) * 365 + (year - 1970) / 4;

    years_ue += Days_Since[month - 1];
    years_ue += ::bcd_to_dec(buffer + 4, 1) - 1;

    years_ue += (!(year % 4) && month >= 3) ? 1 : 0;

    years_ue *= 86400;
    years_ue += ::bcd_to_dec(buffer, 1);
    years_ue += ::bcd_to_dec(buffer+1, 1) * 60;
    years_ue += hours * 60 * 60;
    return years_ue ;
}


