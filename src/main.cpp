#include <Arduino.h>
#include <ESP8266WebServer.h>
#include "pt.h"
#include "async_wait.h"
#include <Wire.h>

#define EEPROM_ADDR  0x50
#define PAGE_SIZE   32
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

byte decToBcd(byte val) {
    return (val / 10 * 16) + (val % 10);
}

byte bcdToDec(byte val) {
    return (val / 16 * 10) + (val % 16);
}

void setTime(byte hour, byte minute, byte second, byte day, byte month, byte year) {
    Wire.beginTransmission(RTC_ADDR);
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
    PT_INIT(&nm_context);
    PT_INIT(&web_server_context);
    Serial.begin(115200);
    Wire.begin();
    SPIFFS.begin();
    stationConnectedHandler = WiFi.onStationModeConnected(wifi_connected_cb);
    disconnectedEventHandler = WiFi.onStationModeDisconnected(wifi_disconnect_cb);
//    /pinMode(D5, OUTPUT);
    //setTime(12, 0, 0, 4, 10, 2024);

    //TODO: оптимизация парсинга json
    //TODO: верификация получнного json для корректного ответа
    server.on("/add_record", []() {
        logRequest();

        char values[4][8];
        const auto payload = server.arg("plain");
        size_t pos = 0;
        size_t pos_old = 1;
        size_t ctr = 0;
        while((pos = payload.indexOf(',', pos+1)) != -1){
            const auto tmp = payload.substring(pos_old, pos);
            strcpy(values[ctr++], tmp.substring(tmp.indexOf(':') == -1 ? 0 : tmp.indexOf(':') + 1).c_str());
            //Serial.println(tmp.substring(tmp.indexOf(':') == -1 ? 0 : tmp.indexOf(':') + 1));

            pos_old = pos;
            //delay(1000);
        }
        if(payload.length() - pos_old) {
            const auto tmp = payload.substring(pos_old, payload.length() - 1);
            strcpy(values[ctr++], tmp.substring(tmp.indexOf(':') == -1 ? 0 : tmp.indexOf(':') + 1).c_str());
            //Serial.println(tmp.substring(tmp.indexOf(':') == -1 ? 0 : tmp.indexOf(':') + 1));
        }

        const char* ptr = values[0];

        Serial.println(((ptr[1] - 48) * 10 + (ptr[2] - 48)) * 3600 +  ((ptr[4] - 48) * 10 + (ptr[5] - 48))* 60);
        //for (const auto& val: values)
        //    Serial.println(val);
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

void byte_write(uint16_t address, uint8_t data){
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(address >> 8);
    Wire.write(address & 0xFF);
    Wire.write(data);
    Wire.endTransmission();
    delay(10);
}

void page_write(uint16_t address, const uint8_t* data, const size_t length){
    for (size_t i = 0; i < length / PAGE_SIZE; ++i, address += PAGE_SIZE) {
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write(address >> 8);
        Wire.write(address & 0xFF);
        Wire.write(data + i * PAGE_SIZE, PAGE_SIZE);
        Wire.endTransmission();
        delay(10);
        Serial.print("Write page #");
        Serial.println(i);
    }
    if((length % PAGE_SIZE) == 0)
        return;
    Serial.print("Write rest [");
    Serial.print(length % PAGE_SIZE);
    Serial.println("]");
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(address >> 8);
    Wire.write(address & 0xFF);
    Wire.write(data + (length / PAGE_SIZE) * PAGE_SIZE, length % PAGE_SIZE);
    Wire.endTransmission();
    delay(10);


//    Wire.beginTransmission(EEPROM_ADDR);
//    Wire.write(address >> 8);
//    Wire.write(address & 0xFF);
//    //Wire.write(reinterpret_cast<const uint8_t*>(data), length);
//    for (int i = 0; i < length; i++) {
//        Wire.write(data[i]);
//    }
//
//
//    Wire.endTransmission();
//    delay(10);
}

uint16_t address_read(){
    uint16_t out = 0;
    Serial.println(Wire.requestFrom(EEPROM_ADDR, sizeof(out)));
    Wire.readBytes(reinterpret_cast<char *>(&out), sizeof(out));
    return out;
}

void random_read(uint16_t address, uint8_t* buffer, int length){
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(address >> 8);
    Wire.write(address & 0xFF);
    Wire.endTransmission();

    Serial.println(Wire.requestFrom(EEPROM_ADDR, length));
//    Wire.readBytes(buffer, length);
    for (int i = 0; i < length; i++) {
        if (Wire.available()) {
            buffer[i] = Wire.read(); // Читаем каждый байт данных
        } else {
            buffer[i] = 0xFF; // Если чтение не удалось, возвращаем 0xFF
        }
    }
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
//    byte error, address;
//    int nDevices;
//
//    Serial.println("Scanning...");
//
//    nDevices = 0;
//    for(address = 1; address < 127; address++ )
//    {
//        // The i2c_scanner uses the return value of
//        // the Write.endTransmisstion to see if
//        // a device did acknowledge to the address.
//        Wire.beginTransmission(address);
//        error = Wire.endTransmission();
//
//        if (error == 0)
//        {
//            Serial.print("I2C device found at address 0x");
//            if (address<16)
//                Serial.print("0");
//            Serial.print(address,HEX);
//            Serial.println("  !");
//
//            nDevices++;
//        }
//        else if (error==4)
//        {
//            Serial.print("Unknown error at address 0x");
//            if (address<16)
//                Serial.print("0");
//            Serial.println(address,HEX);
//        }
//    }
//    if (nDevices == 0)
//        Serial.println("No I2C devices found\n");
//    else
//        Serial.println("done\n");
//
//
//
//
//
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
//Serial.println(address_read(), HEX);
//    const uint8_t dataw[35] = {0x45, 0x33, 0xfc, 0xee, 0x03};
//   page_write(0x0005, dataw, 35);
//
//
//    const int numBytes = 15; // Количество байт для чтения
//    byte data[numBytes]; // Массив для хранения считанных данных
//
//    // Пример чтения 5 байт с адреса 0x0005
//    random_read(0x0000, data, numBytes);
//
//    Serial.print("Прочитанные данные: ");
//    for (int i = 0; i < numBytes; i++) {
//        Serial.print(data[i], HEX); // Выводим данные в шестнадцатеричном формате
//        Serial.print(" ");
//    }
//    Serial.println();
//
//    delay(1000); // Задержка между циклами


}


