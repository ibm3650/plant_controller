#include <Arduino.h>
#include <ESP8266WebServer.h>
#include "pt.h"
#include "async_wait.h"
#include "24c32.h"
#include <Wire.h>


#define RTC_ADDR 0x68
#define MAX_WIFI_TRIES  5
#define SSID "MikroTik-B971FF"
#define PASSWORD "pussydestroyer228"


struct entry_t {
    uint16_t start;
    uint16_t end;
    uint16_t transition_time;
} __attribute__((packed));



static WiFiEventHandler stationConnectedHandler;
static WiFiEventHandler disconnectedEventHandler;
static ESP8266WebServer server(80);
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

#define STRNUM_TO_INT(c, pos)    ((*((c) + (pos) ) - '0') * 10 + (*((c)+ (pos) +1) - '0'))

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



void setup() {
    PT_INIT(&nm_context);
    PT_INIT(&web_server_context);
    Serial.begin(115200);
    Wire.begin();
    SPIFFS.begin();
    stationConnectedHandler = WiFi.onStationModeConnected(wifi_connected_cb);
    disconnectedEventHandler = WiFi.onStationModeDisconnected(wifi_disconnect_cb);


    //TODO: оптимизация парсинга json
    //TODO: верификация получнного json для корректного ответа
    server.on("/add_record", []() {
        logRequest();
        const auto payload = server.arg("plain");
        //Оптимистичный ответ
        server.send(200, "application/json", payload);

        char values[4][8];
        size_t pos = 0;
        size_t pos_old = 1;
        size_t ctr = 0;
        while((pos = payload.indexOf(',', pos+1)) != -1){
            const auto tmp = payload.substring(pos_old, pos);
            strcpy(values[ctr++], tmp.substring(tmp.indexOf(':') == -1 ? 0 : tmp.indexOf(':') + 1).c_str());
            pos_old = pos;
        }
        if(payload.length() - pos_old) {
            const auto tmp = payload.substring(pos_old, payload.length() - 1);
            strcpy(values[ctr++], tmp.substring(tmp.indexOf(':') == -1 ? 0 : tmp.indexOf(':') + 1).c_str());
        }

        Serial.println(strtime_to_mins(values[0]));
        Serial.println(strtime_to_mins(values[1]));
        Serial.println(str_to_int(values[3]));
    });

    server.on("/", [](){
        logRequest();
        File file = SPIFFS.open("/index.html", "r"); //Открываем файл
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

void loop() {

    PT_SCHEDULE(network_monitor, nm_context);
    PT_SCHEDULE(web_server, web_server_context);

//    Wire.beginTransmission(RTC_ADDR);
//    Wire.write(0);  // Начинаем с регистра 0
//    Wire.endTransmission();
//    Serial.println(Wire.requestFrom(RTC_ADDR, 8));
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
////    delay(1000);           // wait 5 seconds for next scan



}


