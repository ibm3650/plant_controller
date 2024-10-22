//
// Created by kandu on 14.10.2024.
//
#include "ntp.h"
#include <WiFiUdp.h>
#include <Arduino.h>
#include <cstring>
#include <optional>




//TODO:Оптимизация инвертирования порядка бит
uint8_t invert_bits(uint8_t value, size_t bits){
    uint8_t reversed = 0;
    for (size_t i = 0; i < bits; ++i) {
        if ((value & (1 << i))) {
            reversed |= 1 << ((bits - 1) - i);
        }
    }
    return reversed;
}

std::time_t ntp_to_timestamp(uint32_t timestamp){
    timestamp = ntohl(timestamp);
    return timestamp - ntp::SINCE_1900;
}

String timestamp_to_string(uint32_t timestamp) {
    timestamp = ntohl(timestamp);
    const std::time_t converted_time = timestamp - 2208988800UL;
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&converted_time)); // Форматирование
    return {buffer};
}

void print_sntp_msg(const ntp::sntp_msg_t& msg) {
    Serial.println("SNTP Message:");
    Serial.print("  Correction: "); Serial.println(invert_bits(msg.correction, 2));
    Serial.print("  Version: "); Serial.println(invert_bits(msg.version, 3));
    Serial.print("  Mode: ");
    switch (invert_bits(msg.mode, 3)) {
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

    double const poll = msg.poll >= 0 ? 1UL << msg.poll : 1.0 / (1UL << -msg.poll);
    Serial.print("  Poll: "); Serial.print(poll);

    double const precision = msg.precision >= 0 ? 1UL << msg.precision : 1.0 / (1UL << -msg.precision);
    Serial.print("  Precision: "); Serial.println(precision);

    Serial.print("  Root Delay: ");Serial.println(ntohs(msg.root_delay.seconds) * 1000.0 +
                                                 ntohs(msg.root_delay.fraction) / 65536.0);

    Serial.print("  Root Dispersion: ");Serial.println(ntohs(msg.root_dispersion.seconds) * 1000.0 +
                                                      ntohs(msg.root_dispersion.fraction) / 65536.0);

    Serial.print(" #Reference ID: ");
    if(msg.stratum >= 2){
        Serial.println(IPAddress(msg.reference_id).toString());
    }

    Serial.print("  Reference Timestamp: ");Serial.println(timestamp_to_string(msg.reference_timestamp.seconds));
    Serial.print("  Originate Timestamp: "); Serial.println(timestamp_to_string(msg.originate_timestamp.seconds));
    Serial.print("  Receive Timestamp: "); Serial.println(timestamp_to_string(msg.receive_timestamp.seconds));
    Serial.print("  Transmit Timestamp: "); Serial.println(timestamp_to_string(msg.transmit_timestamp.seconds));
}

ntp::ntp_client::ntp_client(std::string_view address, uint16_t port) noexcept {
    set_server(address, port);
}

void ntp::ntp_client::set_server(std::string_view address, uint16_t port) noexcept {
    address_ = address;
    port_ = port;
    is_initialized_ = false;
    if(!socket_.begin(LISTENING_PORT)) {
        Serial.println("#NTP No UDP sockets available to use");
        return;
    }
    is_initialized_ = true;
}
//FIXME: Проверка валидности пакета
// TODO(kandu): Сделать асинхронным
[[nodiscard]] bool ntp::ntp_client::sync() noexcept {
    while(socket_.parsePacket() != 0) {
        socket_.flush();
}
    //socket_.stop();
    if (!socket_.beginPacket(address_.c_str(), port_)) {
        Serial.println("#NTP Failed to start packet");
        return false;
    }
    memset(&message_, 0, sizeof(message_));
    message_.mode = static_cast<uint8_t>(MODE::CLIENT);
    message_.version = 4;
    message_.correction = static_cast<uint8_t>(CORRECTION::NOT_SYNC);
   // uint8_t b = 0b11100011;
   // memcpy(&message_,&b,1);
    //Serial.print("SNTP Message:");Serial.println(*reinterpret_cast<uint8_t*>(&message_), BIN);
   // Serial.print("SNTP Message1:");Serial.println(val, BIN);
    size_t packet_size = socket_.write(reinterpret_cast<uint8_t*>(&message_), sizeof(message_));
    if (packet_size == 0) {
        Serial.println("#NTP Failed to write to packet");
        return false;
    }
    if (!socket_.endPacket()) {
        Serial.println("#NTP Failed to send UDP packet");
        return false;
    }
    delay_timer_(connection_timeout_);
    while (delay_timer_ && (packet_size = socket_.parsePacket()) < sizeof(message_));
    if (packet_size < sizeof(message_)) {
        Serial.println("#NTP Timeout waiting for response or packet size is less than expected");
        return false;
    }
    const int len = socket_.read(reinterpret_cast<uint8_t*>(&message_), sizeof(message_));
    last_update_ = millis();

    //print_sntp_msg(message_);
    return true;
}

ntp::UPDATE_STATUS ntp::ntp_client::sync_poll() noexcept {
    if (is_synchronized()) {
        return UPDATE_STATUS::NO_UPDATE;
    }
    return (sync() ? UPDATE_STATUS::UPDATE_SUCCESS : UPDATE_STATUS::UPDATE_FAILED);
}

bool ntp::ntp_client::is_synchronized() const noexcept {
    return (((last_update_ + (update_interval_ * 1000)) >= millis()) && last_update_ != 0);
}

void ntp::ntp_client::set_timezone_offset(uint8_t offset) noexcept {
    time_zone_offset_ = offset;
}

void ntp::ntp_client::set_timeout(size_t timeout) noexcept {
    connection_timeout_ = timeout;
}

void ntp::ntp_client::set_update_interval(size_t interval) noexcept {
    update_interval_ = interval;
}
//FIXME: Учитывать внутреннее состояние для возврата корректного значения
//FIXME: Учитывать устаревание данных
std::optional<std::time_t> ntp::ntp_client::time() const noexcept {
    if(last_update_ == 0){
        return std::nullopt;
    }
    return ntp_to_timestamp(message_.transmit_timestamp.seconds) +
    time_zone_offset_ * 3600 +
    (millis() - last_update_) / 1000;
}


