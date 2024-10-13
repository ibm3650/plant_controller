#include <Arduino.h>
#include <ESP8266WebServer.h>
#include "pt.h"
#include "async_wait.h"
#include "24c32.h"
#include <Wire.h>
#include <WiFiUdp.h>
#include <ctime>

#define STRNUM_TO_INT(c, pos)    ((*((c) + (pos) ) - '0') * 10 + (*((c)+ (pos) +1) - '0'))

#define MAX_WIFI_TRIES  5
#define SSID "MikroTik-B971FF"
#define PASSWORD "pussydestroyer228"

//TODO: mutex for i2c Wire
struct entry_t {
    uint16_t start ;//: 11;
    uint16_t end; //: 11;
    uint16_t transition_time ;//: 9;
    //service vars
    entry_t* next;
    uint8_t deleted;// : 1;
    //uint8_t deleted : 1;//TODO: оптимизировать побитно
} __attribute__((packed));


static WiFiEventHandler stationConnectedHandler;
static WiFiEventHandler disconnectedEventHandler;
static ESP8266WebServer server(80);
static WiFiUDP UDP;
static pt nm_context;
static pt web_server_context;
static uint8_t tries_ctr = 0;


void logMessage(const String& message) {
    Serial.println(message);  // Выводим сообщение в последовательный монитор
}


void logRequest() {
    logMessage("Новый запрос:");
    logMessage("Метод: " + String((server.method() == HTTP_GET) ? "GET" : "POST"));
    logMessage("URI: " + server.uri());
    logMessage("IP-адрес клиента: " + server.client().remoteIP().toString());

    // Вывод параметров запроса, если есть
    if (server.args() > 0) {
        for (int i = 0; i < server.args(); i++) {
            logMessage("Параметр: " + server.argName(i) + " = " + server.arg(i));
        }
    }

    // Вывод заголовков запроса, если есть
    if (server.headers() > 0) {
        for (int i = 0; i < server.headers(); i++) {
            logMessage("Заголовок: " + server.headerName(i) + " = " + server.header(i));
        }
    }

    logMessage("-------------------------");
}

void wifi_disconnect_cb(const WiFiEventStationModeDisconnected& /*event*/){
    PT_SCHEDULE_RESUME(&nm_context);
    ++tries_ctr;
}

void wifi_connected_cb(const WiFiEventStationModeConnected& /*event*/){
    tries_ctr = 0;
}

uint16_t strtime_to_mins(const char* time_str){
    return STRNUM_TO_INT(time_str, 1) * 60 + STRNUM_TO_INT(time_str, 4);
}

int32_t int_pow(int32_t base, int32_t exp){
    if (base == 0) {
        return (exp == 0) ? 1 : 0; // 0^0 = 1, иначе 0
    }
    if (exp == 0) {
        return 1; // любое число в степени 0 = 1
    }

    int32_t accumulator = 1;
    bool is_negative_exp = (exp < 0); // Проверка, является ли экспонента отрицательной

    if (is_negative_exp) {
        exp = -exp; // Преобразование экспоненты в положительную
    }

    // Вычисление степени
    for (int32_t i = 0; i < exp; ++i) {
        accumulator *= base;
    }

    // Если экспонента была отрицательной, возвращаем 1 / результат
    return is_negative_exp ? (1 / accumulator) : accumulator;
}

int32_t str_to_int(const char* str) {
    int sign = 1; // Переменная для хранения знака
    if (*str == '-') {
        str++;
        sign = -1; // Если строка начинается с '-', устанавливаем знак в -1
    }

    int32_t accumulator = 0; // Аккумулятор для результата
    while (*str) { // Пока не достигнут конец строки
        if (*str < '0' || *str > '9') {
            // Если символ не является цифрой, можно обработать ошибку
            return 0; // Или другое значение, сигнализирующее об ошибке
        }

        // Преобразуем символ в число и добавляем к аккумулятору
        accumulator = accumulator * 10 + (*str - '0');
        str++; // Переход к следующему символу
    }

    return accumulator * sign; // Возвращаем результат с учетом знака
}

//TODO: Первый байт на страницу епром обозначает, если ли в этой странице свободные ячейки, например помле удаления узла из списка, для дефргаментации и эффективной вставки. такие блоки ищутся перед вставкой
//TODO: Вставка более чем одного элемента
//TODO: Кеширование записей EEPROM
//TODO: удаление записей EEPROM
//TODO: полная реализация sntp по стандарту rfc
entry_t get_node(uint16_t address=0x0000){
    entry_t tmp{};
    read_random(address, reinterpret_cast<uint8_t *>(&tmp), sizeof(entry_t));
    return tmp;
}

void insert_node(const entry_t& entry, uint16_t address=0x0000){
    if(get_node().deleted)return;
    write_page(address, reinterpret_cast<const uint8_t *>(&entry), sizeof(entry));
}

void delete_node(uint16_t address=0x0000){
    entry_t tmp{};
    read_random(address, reinterpret_cast<uint8_t *>(&tmp), sizeof(entry_t));
    tmp.deleted = true;
    write_page(address, reinterpret_cast<const uint8_t *>(&tmp), sizeof(entry_t));
}

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

#define DS1307_ADDRESS 0x68  // Адрес DS1307

// Функция для преобразования десятичного числа в BCD (Binary Coded Decimal)

uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10 * 16) + (val % 10));
}

// Функция для установки времени на DS1307
void set_time(uint8_t sec, uint8_t min, uint8_t hour, uint8_t day_of_week, uint8_t day_of_month, uint8_t month, uint16_t year) {
    // Переводим значения времени в формат BCD
    uint8_t year_short = year % 100;  // DS1307 использует только последние две цифры года
    sec = dec_to_bcd(sec);
    min = dec_to_bcd(min);
    hour = dec_to_bcd(hour);
    day_of_week = dec_to_bcd(day_of_week);
    day_of_month = dec_to_bcd(day_of_month);
    month = dec_to_bcd(month);
    year_short = dec_to_bcd(year_short);

    // Начинаем передачу данных на DS1307
    Wire.beginTransmission(DS1307_ADDRESS);
    Wire.write(0x00);  // Адрес регистра секунд (начало записи с 0x00)

    // Записываем данные
    Wire.write(sec);            // секунды
    Wire.write(min);            // минуты
    Wire.write(hour);           // часы
    Wire.write(day_of_week);    // день недели (1 - понедельник, 7 - воскресенье)
    Wire.write(day_of_month);   // день месяца
    Wire.write(month);          // месяц
    Wire.write(year_short);     // год (последние две цифры)

    // Завершаем передачу
    Wire.endTransmission();
}
void setup() {
    PT_INIT(&nm_context);
    PT_INIT(&web_server_context);
    Serial.begin(115200);
    Wire.begin();
    SPIFFS.begin();
    UDP.begin(1337);
    stationConnectedHandler = WiFi.onStationModeConnected(wifi_connected_cb);
    disconnectedEventHandler = WiFi.onStationModeDisconnected(wifi_disconnect_cb);
   // set_time(0, 30, 17, 5, 11, 10, 2024);

    //TODO: оптимизация парсинга json
    //TODO: верификация получнного json для корректного ответа
    //TODO: защита от повторной вставки
    //TODO: проверки при добавлении
    //TODO: пв sprintf правильные литералы аргументов или вообще std::format
    server.on("/add_record", []() {
        logRequest();
        const auto prepare = [](const String& str) -> String {
            const size_t delim_pos = str.indexOf(':');
            return str.substring(delim_pos == -1 ? 0 : delim_pos + 1);
        };
        const auto payload = server.arg("plain");
        //TODO:Оптимистичный ответ
        server.send(200, "application/json", payload);

        char values[4][8];
        size_t pos = 0;
        size_t pos_old = 1;
        size_t ctr = 0;
        while((pos = payload.indexOf(',', pos+1)) != -1){
            const auto tmp = payload.substring(pos_old, pos);
            strcpy(values[ctr++], prepare(tmp).c_str());
            pos_old = pos;
        }
        if(payload.length() - pos_old) {
            const auto tmp = payload.substring(pos_old, payload.length() - 1);
            strcpy(values[ctr++], prepare(tmp).c_str());
        }
        insert_node({strtime_to_mins(values[0]),
                strtime_to_mins(values[1]),
                static_cast<uint16_t>(str_to_int(values[3])),
                nullptr,
                false});


//        Serial.println(strtime_to_mins(values[0]));
//        Serial.println(strtime_to_mins(values[1]));
//        Serial.println(str_to_int(values[3]));
    });
    //TODO:получать все записи
    //TODO:оптимальное формирование json
    //TODO:использовать по возможности статичекое выделение памяти
    //TODO:строковыке литералы во флеш памяти
    server.on("/get_records", []() {
        const auto  entry = get_node();
        const char* fmt = R"([{
                "startTime": "%02d:%02d",
                "endTime": "%02d:%02d",
                "smoothTransition": %s,
                "duration": %d
        }])";
        Serial.println(fmt);
        const size_t buffer_size = snprintf(nullptr, 0, fmt,
                                      entry.start / 60,
                                      entry.start % 60,
                                      entry.end / 60,
                                      entry.end % 60,
                                      entry.transition_time == 0 ? "false" : "true",
                                      entry.transition_time);

        String buffer(static_cast<const char*>(nullptr)); //TODO:чтобы не выделял память в куче зря. SSO?
        buffer.reserve(buffer_size + 1);//TODO:проверит есть ли выделение памяти
        snprintf(buffer.begin(), //TODO: потому что в ардуино эта функция возвращает указатель на буфер неконстантным
                 buffer_size + 1,
                 fmt,
                 entry.start / 60,
                 entry.start % 60,
                 entry.end / 60,
                 entry.end % 60,
                 entry.transition_time == 0 ? "false" : "true",
                 entry.transition_time);

        server.send(200, "application/json", buffer.c_str());
    });

    server.on("/", [](){
        logRequest();
        File file = SPIFFS.open("/index.html", "r");
        if(!file)
            return;
        server.streamFile(file, "text/html");
        file.close();
    });

    server.begin();
}






static PT_THREAD(network_monitor) {
    static async_wait delay(0);
    PT_BEGIN(pt);

                while (true) {
                    PT_YIELD_UNTIL(pt, !delay);

                    if (WiFi.status() == WL_CONNECTED) {
                        Serial.print("Connected[");
                        Serial.print(SSID);
                        Serial.print("]");
                        logMessage("IP адрес: " + WiFi.localIP().toString());
                        PT_EXIT(pt);
                    }

                    if (tries_ctr >= MAX_WIFI_TRIES) {
                        Serial.println("Too many tries. Timeout");
                        tries_ctr = 0;
                        WiFi.disconnect();
                        delay(5 * 60 * 1000);
                    }
                    else {
                        if (tries_ctr++ == 0)
                            WiFi.begin(SSID, PASSWORD);
                        Serial.print("Connecting to WIFI [");
                        Serial.print(SSID);
                        Serial.print("] #");
                        Serial.println(tries_ctr);
                        delay(1 * 1000);
                    }
                    PT_YIELD(pt);
                }

    PT_END(pt);
}

//TODO: Вариант не работает с режимом точки доступа
//TODO: Не ждет подключения к ТД или поднятия хотспоста
//TODO: Остановка сервера при простое переподключения
static PT_THREAD(web_server) {
    PT_BEGIN(pt);
                while (true) {
                    server.handleClient();
                    PT_YIELD(pt);
                }
    PT_END(pt);
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


//TODO: калбеки для асинзронной задержки и поллинг нтуреннего цикла
void sntp(){
    sntp_msg message{.version = 4,
                     .mode = 3};
    //msg.transmit_timestamp = htonll(current_time_in_ntp_format()); //TODO;учет времени отправки важен и обязателен
    //msg.transmit_timestamp = htonll(current_time_in_ntp_format()); //TODO;коррекция sntp
//    Serial.println(UDP.beginPacket("pool.ntp.org", 123));
//    Serial.println(UDP.write(reinterpret_cast<uint8_t*>(&message), sizeof(message)));
//    Serial.println(UDP.endPacket());
    Serial.printf("Sending SNTP request to pool.ntp.org on port 123\n");
    if (UDP.beginPacket("pool.ntp.org", 123) == 1) {
        Serial.printf("Packet started successfully.\n");
        if (UDP.write(reinterpret_cast<uint8_t*>(&message), sizeof(message)) > 0) {
            Serial.printf("Packet written successfully. Size: %d bytes\n", sizeof(message));
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
    int packetSize = 0;
    while(delay && (packetSize = UDP.parsePacket()) == 0);

    Serial.println(packetSize);
    if (packetSize) {
        int len = UDP.read(reinterpret_cast<uint8_t*>(&message), sizeof(message));
        Serial.println(packetSize);
        Serial.printf("Received packet from %s:%d\n", UDP.remoteIP().toString().c_str(), UDP.remotePort());
        Serial.printf("Message len : %d\n", len);
        print_sntp_msg(message);
    }
}

#include "ds1307.h"
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
void loop() {
    ds1307::time(nullptr);
//    PT_SCHEDULE(network_monitor, nm_context);
//    PT_SCHEDULE(web_server, web_server_context);
//    if(WiFi.status()==WL_CONNECTED) sntp();
    Wire.beginTransmission(ds1307::RTC_ADDR);
    Wire.write(0);  // Начинаем с регистра 0
    Wire.endTransmission();
    Serial.println(Wire.requestFrom(ds1307::RTC_ADDR, 8U));
    uint8_t buffer[8];
    Wire.readBytes(buffer,8);
    uint64_t accumulator{};
    int Days_Since[] = {0, 31, 59, 90, 120, 151,
                        181, 212, 243, 273, 304, 334};
//    //sec
//    uint8_t tmp = Wire.read();
//    accumulator += ::bcd_to_dec(&tmp, 1);
//    //min
//    tmp = Wire.read();
//    accumulator += ::bcd_to_dec(&tmp, 1) * 60;
//    //hour
//    tmp = Wire.read();
//    accumulator += ::bcd_to_dec(&tmp, 1) * 3600;
//    //day
//    tmp = Wire.read();//skip day
//    //date
//    tmp = Wire.read();
//    accumulator += ::bcd_to_dec(&tmp, 1) * 86400;
//    //month
//    tmp = Wire.read();
//    accumulator += ::bcd_to_dec(&tmp, 1) * 86400;
//    //year
//    tmp = Wire.read();
    int Year_Type = (buffer[6] % 4);
//    if (Year_Type == 0 && buffer[5] >= 3){
//        Leap_Year_Correction_Factor = 1;
//    }
//    else
//    {
//        Leap_Year_Correction_Factor = 0;
//    }
Serial.printf("%02d:%02d:%02d\n", ::bcd_to_dec(buffer , 1),::bcd_to_dec(buffer + 1, 1),::bcd_to_dec(buffer + 2, 1));
    size_t years_ue = ::bcd_to_dec(buffer + 6, 1) + 2000 - 1970;
    years_ue = years_ue * 365.25;

    years_ue += Days_Since[::bcd_to_dec(buffer + 5, 1) - 1];
    years_ue += ::bcd_to_dec(buffer + 4, 1) - 1;

    years_ue += (Year_Type == 0 && ::bcd_to_dec(buffer + 5, 1) >= 3) ? 1 : 0;

    years_ue *= 86400;
    years_ue += ::bcd_to_dec(buffer, 1);
    years_ue += ::bcd_to_dec(buffer+1, 1) *60;
    years_ue += ::bcd_to_dec(buffer+2, 1) *60 * 60;
    Serial.print("Year: ");Serial.println(years_ue);
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
    delay(1000);



}


