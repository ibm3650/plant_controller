#include <Arduino.h>
#include <ESP8266WebServer.h>
#include "pt.h"
#include "async_wait.h"
#include <Wire.h>

#define DS1307_ADDR 0x68
#define MAX_WIFI_TRIES  5
#define SSID "MikroTik-B971FF"
#define PASSWORD "pussydestroyer228"

static WiFiEventHandler stationConnectedHandler;
static WiFiEventHandler disconnectedEventHandler;
static ESP8266WebServer server(80);
static pt nm_context;
static pt web_server_context;
static uint8_t tries_ctr = 0;


void logMessage(String message) {
    Serial.println(message);  // Выводим сообщение в последовательный монитор
}


void logRequest() {
    logMessage("Новый запрос:");
    logMessage("Метод: " + String((server.method() == HTTP_GET) ? "GET" : "POST"));
    logMessage("URI: " + server.uri());
    logMessage("IP-адрес клиента: " + server.client().remoteIP().toString());

    // Вывод параметров запроса, если есть
    if (server.args() > 0) {
        for (uint8_t i = 0; i < server.args(); i++) {
            logMessage("Параметр: " + server.argName(i) + " = " + server.arg(i));
        }
    }

    // Вывод заголовков запроса, если есть
    if (server.headers() > 0) {
        for (uint8_t i = 0; i < server.headers(); i++) {
            logMessage("Заголовок: " + server.headerName(i) + " = " + server.header(i));
        }
    }

    logMessage("-------------------------");
}



void wifi_disconnect_cb(const WiFiEventStationModeDisconnected& event){
    PT_SCHEDULE_RESUME(&nm_context);
    ++tries_ctr;
}

void wifi_connected_cb(const WiFiEventStationModeConnected& event){
    tries_ctr = 0;
}

byte decToBcd(byte val) {
    return (val / 10 * 16) + (val % 10);
}

byte bcdToDec(byte val) {
    return (val / 16 * 10) + (val % 16);
}

void setTime(byte hour, byte minute, byte second, byte day, byte month, byte year) {
    Wire.beginTransmission(DS1307_ADDR);
    Wire.write(0);  // Устанавливаем указатель на 0 регистр
    Wire.write(decToBcd(second));  // Секунды
    Wire.write(decToBcd(minute));  // Минуты
    Wire.write(decToBcd(hour));    // Часы
    Wire.write(1);                 // День недели (не используется)
    Wire.write(decToBcd(day));     // День
    Wire.write(decToBcd(month));   // Месяц
    Wire.write(decToBcd(year - 2000));  // Год (DS1307 хранит год как два последних символа)
    Wire.endTransmission();
}
void setup() {
    pinMode(D5, OUTPUT);
    Serial.begin(115200);
    Wire.begin();
    setTime(12, 0, 0, 4, 10, 2024);
    PT_INIT(&nm_context);
    PT_INIT(&web_server_context);
    stationConnectedHandler = WiFi.onStationModeConnected(wifi_connected_cb);
    disconnectedEventHandler = WiFi.onStationModeDisconnected(wifi_disconnect_cb);
    if(!SPIFFS.begin()){ //Инициализируем SPIFFS
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }
    server.on("/addRecord", []() {
        logRequest();  // Логгируем данные запроса
        Serial.println(server.arg("plain"));
        server.send(200);
    });

    server.on("/", [](){
        Serial.print("Обработчик корня");
        logRequest();  // Логгируем данные запроса
        File file = SPIFFS.open("/index.html", "r"); //Открываем файл
        if(!file){ //Проверка наличия
            Serial.println("Failed to open file for reading");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
        server.send(200, "text/html", "<h1>Привет! Это веб-сервер на ESP8266!</h1>");
    });
    server.begin();

    //    String data;
    //    File file = SPIFFS.open("/index.html", "r"); //Открываем файл
    //    if(!file){ //Проверка наличия
    //        Serial.println("Failed to open file for reading");
    //        return;
    //    }
    //    data = file.readString(); //Конвертируем данные в нормальное представление
    //    uint8_t data1[data.length()];
    //    data.getBytes(data1, data.length()); //168
    //    file.close(); //Завершаем работу с файлом
    //    Serial.println(data); //Выводим информацию в монитор порта
}


//Вариант не работает с режимом точки доступа
static PT_THREAD(web_server) {
    PT_BEGIN(pt);
    while (true) {
        //PT_YIELD_UNTIL(pt, WiFi.status() != WL_CONNECTED);
        server.handleClient();
        PT_YIELD(pt);
    }

    PT_END(pt);

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



void loop() {
//    Wire.beginTransmission(DS1307_ADDR);
//    Wire.write(0);  // Начинаем с регистра 0
//    Wire.endTransmission();
//    Serial.println(Wire.requestFrom(DS1307_ADDR, 8));
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
//    delay(1000);           // wait 5 seconds for next scan
    PT_SCHEDULE(network_monitor, nm_context);
    PT_SCHEDULE(web_server, web_server_context);
}
