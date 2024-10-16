
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include "pt.h"
#include "async_wait.h"
#include "24c32.h"
#include "ds1307.h"
#include "ntp.h"
#include <Wire.h>
//#include <WiFiUdp.h>
#include <ctime>

#define STRNUM_TO_INT(c, pos)    ((*((c) + (pos) ) - '0') * 10 + (*((c)+ (pos) +1) - '0'))

#define MAX_WIFI_TRIES  5
#define SSID "MikroTik-B971FF"
#define PASSWORD "pussydestroyer228"
//#define SSID "S21"
//#define PASSWORD "rxzn8231"
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
//static WiFiUDP UDP;
static pt nm_context;
static pt web_server_context;
static pt led_context;
static pt entriess_context;
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
entry_t cache_entries[4]{};
size_t cache_ctr = 0;
//TODO: Первый байт на страницу епром обозначает, если ли в этой странице свободные ячейки, например помле удаления узла из списка, для дефргаментации и эффективной вставки. такие блоки ищутся перед вставкой
//TODO: Вставка более чем одного элемента
//TODO: Кеширование записей EEPROM
//TODO: удаление записей EEPROM
//TODO: полная реализация time по стандарту rfc
entry_t get_node(uint16_t address=0x0000){
    return cache_entries[0];
    entry_t tmp{};
    read_random(address, reinterpret_cast<uint8_t *>(&tmp), sizeof(entry_t));
    return tmp;
}

void insert_node(const entry_t& entry, uint16_t address=0x0000){
    Serial.println("Inserting");
    Serial.println(entry.start);
    cache_entries[cache_ctr] = entry;cache_ctr++;return;
    if(get_node().deleted)return;
    write_page(address, reinterpret_cast<const uint8_t *>(&entry), sizeof(entry));
}

void delete_node(uint16_t address=0x0000){
    entry_t tmp{};
    read_random(address, reinterpret_cast<uint8_t *>(&tmp), sizeof(entry_t));
    tmp.deleted = true;
    write_page(address, reinterpret_cast<const uint8_t *>(&tmp), sizeof(entry_t));
}



static bool state = false;
static size_t del_t = 0;
//TODO: учесть, что переход может произойти раньше чем счетчик достигнет максимума
void set_led(bool state_, size_t transition = 0) {
   // if (state == state_) {
   //     return;
   // }
    state = state_;
    del_t = transition / 0xFF;
    PT_SCHEDULE_RESUME(&led_context);
}
//TODO: Кеширование всех записей EEPROM
//TODO: Сортировка кешированных записей EEPROM
void setup() {
    PT_INIT(&nm_context);
    PT_INIT(&web_server_context);
    PT_INIT(&led_context);
    PT_INIT(&entriess_context);
    Serial.begin(115200);
    Wire.begin();
    SPIFFS.begin();
    cache_entries[cache_ctr++] = get_node();
    cache_entries[cache_ctr].start -= cache_entries[0].transition_time;
    cache_entries[cache_ctr].end -= cache_entries[0].transition_time;
    //UDP.begin(1337);
    stationConnectedHandler = WiFi.onStationModeConnected(wifi_connected_cb);
    disconnectedEventHandler = WiFi.onStationModeDisconnected(wifi_disconnect_cb);
    pinMode(D5, OUTPUT);
    //set_led(1, 1000);
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
    //TODO:использовать кеш
    //Оптимизировать эту функцию
    server.on("/get_records",  []() {
        //auto const   entry = cache_entries[0];
        const auto  entry = get_node();
        const char* fmt = R"([{
                "startTime": "%02d:%02d",
                "endTime": "%02d:%02d",
                "smoothTransition": %s,
                "duration": %d
        }])";

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
        Serial.println(buffer.c_str());
        server.send(200, "application/json", buffer.c_str());
    });
    server.on("/", [](){

       //state = !state;
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
//TODO: Работа с i2c тоже протопоток

static PT_THREAD(web_server) {
    PT_BEGIN(pt);
    while (true) {
        server.handleClient();
        PT_YIELD(pt);
    }
    PT_END(pt);
}

//TODO: Ощибка если статус меняется во время выполнения предыдущего запроса
static PT_THREAD(led_state){
    static uint8_t i = 0xFF;
    static bool current_state = false;
    static async_wait delay{0};
    if(i == 0xFF) {
        if(state == current_state)
            PT_EXIT(pt);
        delay(del_t);
        i = 0;
    }

    //Serial.printf("LED VALUE: %d#%d\n", i,__LINE__);
    PT_BEGIN(pt);
    for (; i < 0xFF; ++i) {
       // Serial.printf("LED VALUE: %d#%d\n", i,__LINE__);
        analogWrite(D5, current_state ? 0xFF - i : i);
        PT_YIELD_UNTIL(pt, !delay);
        delay(del_t);
        Serial.printf("LED VALUE: %d#%d\n", i, current_state);
    }
//                if(state == current_state) {
//                    current_state = !current_state;
//                    PT_YIELD(pt);
//                }
  //  Serial.printf("LED VALUE: %d#%d\n", i,__LINE__);
    current_state = state;
                //PT_EXIT(pt);
    PT_END(pt);
}

////функция которая кооректирует время ds1307 по времени из ntp раз в сутки
//void f(){
//    static time_t last_time = 0;
//    if(ds1307::time(nullptr) - last_time >= 24 * 60 * 60){
//        ds1307::set_time(ntp::time());
//        last_time = ds1307::time(nullptr);
//    }
//}

static PT_THREAD(entries_processing){
    const std::time_t time_tmp = ntp::time() + TIMEZONE * 60 * 60;
    //const std::time_t time_tmp = ds1307::time(nullptr) + TIMEZONE * 60 * 60;
    const auto curr_min = std::gmtime(&time_tmp)->tm_hour * 60 + std::gmtime(&time_tmp)->tm_min;
  //  Serial.println(std::ctime(&time_tmp));
   // Serial.println(ds1307::time(nullptr));
    //Serial.println(curr_min);
   // Serial.println(time_tmp);
    PT_BEGIN(pt);

    while(true){
        for(size_t i = 0; i < cache_ctr; ++i){
            if(cache_entries[i].start == 0 && cache_entries[i].end == 0)
                continue;
            //TODO: оптимизировать/переписать/помечать задачу как запущенную или отпавлять в конец списка

            if(curr_min  >= (cache_entries[i].end - cache_entries[i].transition_time)){
                set_led(false, cache_entries[i].transition_time * 60 * 1000);
                Serial.println("LED OFF");
                continue;
            }
            if(curr_min  >= (cache_entries[i].start - cache_entries[i].transition_time)){
                set_led(true, cache_entries[i].transition_time * 60 * 1000);
                Serial.println("LED ON");
            }
        }
        PT_YIELD(pt);
    }
    PT_END(pt);
}

//void led_state(bool state, size_t transition = 0){
//    static bool current_state = false;
//    if (state == current_state)
//        return;
//    if(transition == 0) {
//        analogWrite(D5, state ? 0xFF : 0x00);
//        current_state = state;
//        return;
//    }
//
//    const size_t del_t = transition / 0xFF;
//    for (int i = 0; i < 0xFF; ++i) {
//        analogWrite(D5, current_state ? i : 0xFF - i);
//        delay(del_t);
//    }
//    current_state = state;
//}


void loop() {
    //static bool current_state = false;
    PT_SCHEDULE(led_state, led_context);
    PT_SCHEDULE(network_monitor, nm_context);
    if(WiFi.status() != WL_CONNECTED)
        return;
    PT_SCHEDULE(web_server, web_server_context);
    //TODO: отвязать от сети
    PT_SCHEDULE(entries_processing, entriess_context);


   // Serial.println(entriess_context.is_stoped);
    //led_state(current_state, 1000);
   // current_state = !current_state ;
   // delay(1000);
//    if(WiFi.status()==WL_CONNECTED) {
//        Serial.print("RTC: ");
//        Serial.println(ds1307::time(nullptr));
//        Serial.print("NTP: ");
//       Serial.println(ntp::time());
//    }
}


