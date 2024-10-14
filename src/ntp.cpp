//
// Created by kandu on 14.10.2024.
//
#include "ntp.h"
#include "async_wait.h"
#include <WiFiUdp.h>
#include <Arduino.h>
//TODO: работа с BCD классом
//TODO: работа с EEPROM классом
//TODO:const char fmt[] = "sqrt(2) = %f";
//int sz = snprintf(NULL, 0, fmt, sqrt(2));
//char buf[sz + 1]; // note +1 for terminating null byte
//snprintf(buf, sizeof buf, fmt, sqrt(2));

int minutes_to_time(uint16_t minutes, char* out, size_t buffer_size) {
    //TODO:sprintf_s???
    return snprintf(out, buffer_size, R"("%02d:%02d")", minutes / 60, minutes % 60);
}

enum CORRECTION{
    NO_WARN,
    LM_61,
    LM_59,
    ALARM
};


enum VERSION{
    RESERVED,
    SYMMETRIC_ACTIVE,
    SYMMETRIC_PASSIVE,
    CLIENT,
    SERVER,
    BROADCAST,
    RESERVED0,
    RESERVED1
};


struct sntp_msg {
    uint8_t correction : 2;
    uint8_t version : 3;
    uint8_t mode : 3;
    uint8_t stratum;
    uint8_t poll;
    int8_t precision;
    int32_t root_delay;
    uint32_t root_dispersion;
    uint32_t reference_id;
    uint64_t reference_timestamp;
    uint64_t originate_timestamp;
    uint64_t receive_timestamp;
    uint64_t transmit_timestamp;
} __attribute__((packed));


String timestamp_to_string(uint64_t timestamp) {
    // SNTP использует временные метки в формате "с 1900 года"
    const uint64_t seconds_since_1900 = 2208988800ULL; // Время в секундах с 1 января 1900 года
    uint64_t seconds_since_1970 = timestamp - seconds_since_1900;

    time_t t = seconds_since_1970; // Конвертация в стандартный формат времени
    char buffer[20]; // Буфер для форматированной строки
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&t)); // Форматирование
    return {buffer};
}

// Функция для печати sntp_msg с расшифровкой значений
void print_sntp_msg(const sntp_msg& msg) {
    Serial.println("SNTP Message:");
    Serial.print("  Correction: "); Serial.println(msg.correction);
    Serial.print("  Version: "); Serial.println(msg.version);
    Serial.print("  Mode: ");
    switch (msg.mode) {
        case 0: Serial.println("Reserved"); break;
        case 1: Serial.println("Symmetric Active"); break;
        case 2: Serial.println("Symmetric Passive"); break;
        case 3: Serial.println("Client"); break;
        case 4: Serial.println("Server"); break;
        case 5: Serial.println("Broadcast"); break;
        case 6: Serial.println("Reserved for NTP control message"); break;
        case 7: Serial.println("Reserved for private use"); break;
        default: Serial.println("Unknown"); break;
    }
    Serial.print("  Stratum: "); Serial.println(msg.stratum);
    Serial.print("  Poll: "); Serial.println(msg.poll);
    Serial.print("  Precision: "); Serial.println(msg.precision);
    Serial.print("  Root Delay: "); Serial.println(msg.root_delay);
    Serial.print("  Root Dispersion: "); Serial.println(msg.root_dispersion);
    Serial.print("  Reference ID: 0x"); Serial.println(msg.reference_id, HEX);
    Serial.print("  Reference Timestamp: "); Serial.println(timestamp_to_string(msg.reference_timestamp));
    Serial.print("  Originate Timestamp: "); Serial.println(timestamp_to_string(msg.originate_timestamp));
    Serial.print("  Receive Timestamp: "); Serial.println(timestamp_to_string(msg.receive_timestamp));
    Serial.print("  Transmit Timestamp: "); Serial.println(timestamp_to_string(msg.transmit_timestamp));
}
static WiFiUDP UDP;
//TODO: коррекция SNTP, согласование с сервером
//TODO: калбеки для асинзронной задержки и поллинг нтуреннего цикла
void sntp(){
    sntp_msg message{.version = 4,
            .mode = 3};
   // msg.transmit_timestamp = htonll(current_time_in_ntp_format()); //TODO;учет времени отправки важен и обязателен
    //msg.transmit_timestamp = htonll(current_time_in_ntp_format()); //TODO;коррекция sntp
//    Serial.println(UDP.beginPacket("pool.ntp.org", 123));
//    Serial.println(UDP.write(reinterpret_cast<uint8_t*>(&message), sizeof(message)));
//    Serial.println(UDP.endPacket());
    int packetSize = 0;
    UDP.begin(1337);
    Serial.printf("Sending SNTP request to pool.ntp.org on port 123\n");
    if (UDP.beginPacket("pool.ntp.org", 123) == 1) {
        Serial.printf("Packet started successfully.\n");
        if ((packetSize =UDP.write(reinterpret_cast<uint8_t*>(&message), sizeof(message))) > 0) {
            Serial.printf("Packet written successfully. Size: %d bytes\n",packetSize);
            if (!UDP.endPacket()) {
                Serial.println("Failed to send UDP packet");
                return;
            }
            Serial.println("Packet sent.\n");
        } else {
            Serial.println("Failed to write to packet.\n");
            return;
        }
    } else {
        Serial.println("Failed to start packet.\n");
        return;
    }
    async_wait delay(1000);

    while(delay && (packetSize = UDP.parsePacket()) == 0);

    Serial.println(packetSize);
    if (packetSize) {
        int len = UDP.read(reinterpret_cast<uint8_t*>(&message), sizeof(message));
        Serial.println(packetSize);
        Serial.printf("Received packet from %s:%d\n", UDP.remoteIP().toString().c_str(), UDP.remotePort());
        Serial.printf("Message len : %d\n", len);
        print_sntp_msg(message);
    }
    //UDP.stop();
}