//
// Created by kandu on 14.10.2024.
//
#include "ntp.h"
#include "async_wait.h"
#include <WiFiUdp.h>
#include <Arduino.h>

struct sntp_msg_t {
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

static WiFiUDP UDP;

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






uint8_t reverseBits(uint8_t num, uint8_t bitCount) {
    uint8_t reversed = 0;
    for (uint8_t i = 0; i < bitCount; ++i) {
        if ((num & (1 << i))) {
            reversed |= 1 << ((bitCount - 1) - i);
        }
    }
    return reversed;
}


uint64_t ntohll(uint64_t value) {
    return ((uint64_t) ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
}
String timestamp_to_string(uint64_t timestamp) {
  //  timestamp = ntohll(timestamp);
    // SNTP использует временные метки в формате "с 1900 года"
   // timestamp = timestamp & 0xFFFFFFFF;
   timestamp = ntohl(timestamp & 0xFFFFFFFF);
   // timestamp = (timestamp & 0xFFFFFFFF00000000)>>32;
    const uint64_t seconds_since_1900 = 2208988800ULL; // Время в секундах с 1 января 1900 года
    uint64_t seconds_since_1970 = timestamp - seconds_since_1900;

    time_t t = seconds_since_1970; // Конвертация в стандартный формат времени
    char buffer[20]; // Буфер для форматированной строки
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&t)); // Форматирование
    return {buffer};
}

//uint32_t ntohl(uint32_t value) {
//    return ((uint64_t) ntohl(value & 0xFFFF) << 16) | ntohl(value >> 16);
//}
// Функция для печати sntp_msg_t с расшифровкой значений
void print_sntp_msg(const sntp_msg_t& msg) {
    Serial.println("SNTP Message:");
    Serial.print("#  Correction: "); Serial.println(msg.correction);
    Serial.print("#  Version: "); Serial.println(msg.version);
    Serial.print("#  Mode: ");

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
    Serial.print("  Poll: "); Serial.print(msg.poll >= 0 ? 1UL << msg.poll : (double)1.0 / (1UL << -msg.poll));Serial.println(" sec");
    Serial.print("#  Precision: "); Serial.println(msg.precision >= 0 ? 1UL << msg.precision : (double)1.0 / (1UL << -msg.precision));
    Serial.print("  Root Delay: "); Serial.printf("[%d] ,  %.3f sec\n",
                                                  msg.root_delay,
//                                                  (ntohl(msg.root_delay) & 0xFFFF0000) >> 16,
//                                                  ntohl(msg.root_delay) & 0x0000FFFF
                                                  ntohs(msg.root_delay & 0x0000FFFF) * 1000.0 +
                                                  ( ntohs((msg.root_delay & 0xFFFF0000) >> 16))  / 65536.0
                                                  );
    Serial.print("  Root Dispersion: "); Serial.printf("[%d] ,  %.3f\n sec",
                                                       msg.root_dispersion,
                                                       ntohs(msg.root_dispersion & 0x0000FFFF) * 1000.0 +
                                                       (ntohs((msg.root_dispersion & 0xFFFF0000) >> 16))  / 65536.0
    );
    if(msg.stratum >= 2){

        Serial.print("  Reference ID: "); Serial.println(IPAddress(msg.reference_id).toString());
//        Serial.print("  Reference ID: 0x"); Serial.println(msg.reference_id, HEX);
    }
    else if(msg.stratum == 0){
        //4 ascii symbols. kiss code}
    }
    else if(msg.stratum == 1) {//4 ascii symbols. list from IANA         }
    }
    Serial.print("  Reference Timestamp: ");
    Serial.println(timestamp_to_string(msg.reference_timestamp));
    Serial.print("  Originate Timestamp: "); Serial.println(timestamp_to_string(msg.originate_timestamp));
    Serial.print("  Receive Timestamp: "); Serial.println(timestamp_to_string(msg.receive_timestamp));
    Serial.print("  Transmit Timestamp: "); Serial.println(timestamp_to_string(msg.transmit_timestamp));
    //Serial.print("  Transmit Timestamp: "); Serial.println(msg.transmit_timestamp-2208988800ULL);
}



inline uint32_t _merge(uint8_t* buf) {
    return (buf[0] << 8) | buf[1];
}

bool isudp = false;
//TODO: коррекция SNTP, согласование с сервером
//TODO: калбеки для асинзронной задержки и поллинг нтуреннего цикла
std::time_t ntp::time(std::time_t *arg) {
    sntp_msg_t message{.correction = NOT_SYNC,
                     .version = 4,
                     .mode = CLIENT};

//    uint8_t ntpResponse[] = {
//            0x34, 0x02, 0x03, 0xE8, 0x00, 0x00, 0x10, 0x10,
//            0x00, 0x00, 0x08, 0x8A, 0x81, 0xFA, 0x23, 0xDE,
//            0xEA, 0xB8, 0xE7, 0x89, 0x90, 0xF3, 0x7F, 0x44,
//            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
//            0xEA, 0xB8, 0xEE, 0x7B, 0xE7, 0xEA, 0x35, 0x21,
//            0xEA, 0xB8, 0xEE, 0x7B, 0xE7, 0xEB, 0xBD, 0x8A
//    };
//
//    memcpy(&message, ntpResponse, 48);
//    print_sntp_msg(message);
//    delay(1000);
   // msg.transmit_timestamp = htonll(current_time_in_ntp_format()); //TODO;учет времени отправки важен и обязателен
    //msg.transmit_timestamp = htonll(current_time_in_ntp_format()); //TODO;коррекция time
//    Serial.println(UDP.beginPacket("pool.ntp.org", 123));
//    Serial.println(UDP.write(reinterpret_cast<uint8_t*>(&message), sizeof(message)));
//    Serial.println(UDP.endPacket());
    int packetSize = 0;
    if(!isudp) {
        UDP.begin(8888);
        isudp=true;
    }
   // UDP.setTimeout(2500);
    IPAddress ip;
    ip.fromString("129.250.35.250");
    uint8_t buf[48] = {0b11100011};
   // Serial.printf("Sending SNTP request to pool.ntp.org on port 123\n");
    if (UDP.beginPacket(ip, 123) == 1) {

   // if (UDP.beginPacket("pool.ntp.org", 123) == 1) {
      //  Serial.printf("Packet started successfully.\n");

       // if ((packetSize = UDP.write(buf, 48)) > 0) {
        if ((packetSize = UDP.write(reinterpret_cast<uint8_t*>(&message), sizeof(message))) > 0) {
         //   Serial.printf("Packet written successfully. Size: %d bytes\n",packetSize);
            if (!UDP.endPacket()) {
            //    Serial.println("Failed to send UDP packet");
                return -1;
            }
         //   Serial.println("Packet sent.\n");
        } else {
        //    Serial.println("Failed to write to packet.\n");
            return -1;
        }
    } else {
      //  Serial.println("Failed to start packet.\n");
        return -1;
    }
    async_wait delayt(1000);


    unsigned long startTime = millis();
    while (delayt && (packetSize = UDP.parsePacket()) != 48) {
//        if (millis() - startTime > 2000) { // Устанавливаем тайм-аут 2 секунды
//            Serial.println("Timeout waiting for response.\n");
//            return;
//        }
    }
  //  delay(1000);
    //packetSize = UDP.parsePacket();
   // Serial.println(packetSize);
    if (packetSize) {
        int len = UDP.read(buf, sizeof(message));
        uint32_t unix = ((_merge(buf + 40) << 16) | _merge(buf + 42)) - 2208988800UL;
        //int len = UDP.read(reinterpret_cast<uint8_t*>(&message), sizeof(message));
        //Serial.println(packetSize);
      //  Serial.printf("Received packet from %s:%d\n", UDP.remoteIP().toString().c_str(), UDP.remotePort());
      //  Serial.printf("Message len : %d\n", len);

//       for (int i = 47; i >= 0 ; --i) {
//            reinterpret_cast<uint8_t*>(&message)[47-i] = buf[i];
//        }
//        for (int i = 0; i < 48; ++i) {
//            Serial.print(buf[i], HEX);
//            Serial.print(" ");
//        }


memcpy(&message, buf, 48);
      //  print_sntp_msg(message);
      // delay(1000);
        message.receive_timestamp = ntohl(message.receive_timestamp & 0xFFFFFFFF);
        // timestamp = (timestamp & 0xFFFFFFFF00000000)>>32;
        const uint64_t seconds_since_1900 = 2208988800ULL; // Время в секундах с 1 января 1900 года
        return message.receive_timestamp - seconds_since_1900;
      //  print_sntp_msg(message);
        //print_sntp_msg(*reinterpret_cast<sntp_msg_t*>(buf));
       //Serial.print("NTP: ");Serial.println(unix);
       //Serial.print("NTP: ");Serial.println(ntohll(reinterpret_cast<sntp_msg_t*>(buf)->transmit_timestamp) - 2208988800UL);
       // message.transmit_timestamp = message.transmit_timestamp & 0x00000000FFFFFFFF;
      //  message.transmit_timestamp = (message.transmit_timestamp & 0xFFFFFFFF00000000) >> 32;
      // Serial.print("NTP: ");Serial.println(ntohll(message.transmit_timestamp) - 2208988800UL);
      // Serial.print("NTP: ");Serial.println(message.transmit_timestamp - 2208988800UL);
    }
    //WiFiUDP::stopAll();
    return -1;
}