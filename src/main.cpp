#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <ctime>
#include "pt.h"
#include "async_wait.h"
#include "24c32.h"
#include "ds1307.h"
#include "ntp.h"
#include "debug.h"

template<typename T, typename... args_pack>
void print_variadic(const T& first, args_pack... args) {
    Serial.print(first);
    Serial.print(' ');
    if constexpr (sizeof...(args) > 0) {
        print_variadic(args...);
    }
    else {
        Serial.println();
    }
}

template<typename... args_pack>
void log_message(LogLevel level, const char* file, int line, args_pack... message) {
    if (level < CURRENT_LOG_LEVEL) return;

    String level_str;
    switch (level) {
        case LogLevel::DEBUG: level_str = "DEBUG"; break;
        case LogLevel::INFO: level_str = "INFO"; break;
        case LogLevel::WARNING: level_str = "WARNING"; break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
    }

    unsigned long current_time = millis();  // Время в миллисекундах с момента старта
    Serial.print(level_str);
    Serial.print(" [");
    Serial.print(file);
    Serial.print(":");
    Serial.print(line);
    Serial.print("] ");
    Serial.print("Time: ");
    Serial.print(current_time / 1000);  // Время в секундах
    Serial.print("s: ");
    print_variadic(message...);
}



#define PT_THREAD_DECL(name)    static pt name##_context{0, #name, false};\
                                static char name(struct pt* pt);

#ifdef PT_THREAD
#undef PT_THREAD
#define PT_THREAD(name) char name(struct pt* pt)
#endif

#ifdef PT_SCHEDULE
#undef PT_SCHEDULE
#define PT_SCHEDULE(name) if(!name##_context.is_stoped)name(&name##_context);
#endif


#ifdef PT_SCHEDULE_RESUME
#undef PT_SCHEDULE_RESUME
#define PT_SCHEDULE_RESUME(name) name##_context.is_stoped=false;
#endif
namespace {
    constexpr auto SSID = "MikroTik-B971FF";
    constexpr auto PASSWORD = "pussydestroyer228";
    constexpr uint8_t MAX_WIFI_TRIES = 15;
    constexpr uint8_t CACHE_SIZE = 16;
    constexpr uint16_t WEB_PORT = 80;


    //TODO: оптимизировать побитно
    //TODO: mutex for i2c Wire
    struct entry_t {
        uint16_t start ;//: 11;
        uint16_t end; //: 11;
        uint16_t transition_time ;//: 9;
        //service vars
        entry_t* next;
        uint8_t deleted;// : 1;
        //uint8_t deleted : 1;
    } __attribute__((packed));

    bool led_state = false;
    uint8_t tries_ctr = 0;
    uint32_t led_transition_delay = 0;
    entry_t cache_entries[CACHE_SIZE]{};
    uint8_t cache_ctr = 0;
    ESP8266WebServer server(WEB_PORT);

    //Protothreads contexts
    //pt nm_context{};
    //pt web_server_context{};
    //pt led_context{};
    //pt entries_context{};
} // namespace

[[maybe_unused]] static int32_t str_to_int(const char* str);

[[maybe_unused]] static uint16_t strtime_to_minutes(const char* time_str);
static const char* get_mime_type(const String& filename);
static entry_t parse_task_json(const String& json);
String create_json(const entry_t* entries, size_t count);
//TODO: учесть, что переход может произойти раньше чем счетчик достигнет максимума
void set_led(bool state, size_t transition = 0);
void wifi_disconnect_cb(const WiFiEventStationModeDisconnected& event);
void wifi_connected_cb(const WiFiEventStationModeConnected& event);
//TODO: оптимизация парсинга json
//TODO: верификация получнного json для корректного ответа
//TODO: защита от повторной вставки
//TODO: проверки при добавлении
//TODO: пв sprintf правильные литералы аргументов или вообще std::format
//TODO:Оптимистичный ответ
void web_add_record_cb();
//TODO:получать все записи
//TODO:оптимальное формирование json
//TODO:использовать по возможности статичекое выделение памяти
//TODO:строковыке литералы во флеш памяти
//TODO:использовать кеш
//TODO:Оптимизировать эту функцию
void web_get_record_cb();
void web_index_cb();
PT_THREAD_DECL(network_manager);
//TODO: Вариант не работает с режимом точки доступа
//TODO: Не ждет подключения к ТД или поднятия хотспоста
//TODO: Остановка сервера при простое переподключения
//TODO: Работа с i2c тоже протопоток
PT_THREAD_DECL(web_server);
//TODO: Ощибка если статус меняется во время выполнения предыдущего запроса
PT_THREAD_DECL(led_control);
//TODO: отвязать от сети
PT_THREAD_DECL(entries_processing);
//TODO: Первый байт на страницу епром обозначает, если ли в этой странице свободные ячейки, например помле удаления узла из списка, для дефргаментации и эффективной вставки. такие блоки ищутся перед вставкой
//TODO: Вставка более чем одного элемента
//TODO: Кеширование записей EEPROM
//TODO: удаление записей EEPROM
//TODO: полная реализация time по стандарту rfc
entry_t get_node(uint16_t address){
    if(cache_ctr == 0){
        LOG_INFO("Cache is empty");
        return {};
    }
    LOG_DEBUG("Getting node", cache_entries[address].start, cache_entries[address].end, cache_entries[address].transition_time);
    return cache_entries[address];
}


void insert_node(const entry_t& entry){
    LOG_DEBUG("Inserting node", entry.start, entry.end, entry.transition_time);
    if(cache_ctr >= CACHE_SIZE){
        LOG_INFO("Cache is full");
        return;
    }
    cache_entries[cache_ctr] = entry;
    cache_ctr++;
}

void delete_node(uint16_t address=0x0000){
    entry_t tmp{};
    read_random(address, reinterpret_cast<uint8_t *>(&tmp), sizeof(entry_t));
    tmp.deleted = true;
    write_page(address, reinterpret_cast<const uint8_t *>(&tmp), sizeof(entry_t));
}

//TODO: Кеширование всех записей EEPROM
//TODO: Сортировка кешированных записей EEPROM
void setup() {
    Serial.begin(115200);
    Wire.begin();
    SPIFFS.begin();
    pinMode(D5, OUTPUT);
    WiFi.onStationModeConnected(wifi_connected_cb);
    WiFi.onStationModeDisconnected(wifi_disconnect_cb);
    server.on("/add_record", web_add_record_cb);
    server.on("/get_records",  web_get_record_cb);
    server.on("/", web_index_cb);
    server.onNotFound([]() {
        LOG_DEBUG("Request", server.uri());
        File file = SPIFFS.open(server.uri(), "r");
        if (!file) {
            server.send(404, "text/plain", "File not found");
            LOG_INFO("File not found", server.uri());
            return;
        }
        server.streamFile(file, get_mime_type(server.uri()));
        file.close();
    });
    server.begin();
    //PT_INIT(&nm_context);
    //PT_INIT(&web_server_context);
    //PT_INIT(&led_context);
    //PT_INIT(&entries_context);


}

////функция которая кооректирует время ds1307 по времени из ntp раз в сутки
//void f(){
//    static time_t last_time = 0;
//    if(ds1307::time(nullptr) - last_time >= 24 * 60 * 60){
//        ds1307::set_time(ntp::time());
//        last_time = ds1307::time(nullptr);
//    }
//}

void loop() {
    PT_SCHEDULE(led_control);
    PT_SCHEDULE(network_manager);
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    PT_SCHEDULE(web_server);
    PT_SCHEDULE(entries_processing);
}

[[maybe_unused]] int32_t str_to_int(const char* str) {
    int8_t sign = 1;
    if (*str == '-') {
        str++;
        sign = -1;
    }
    int32_t accumulator = 0;
    while (*str) {
        if(*str < '0' || *str > '9') {
            return 0;
        }
        assert(*str >= '0' && *str <= '9');
        accumulator = accumulator * 10 + (*str - '0'); // NOLINT(*-magic-numbers)
        str++;
    }
    return accumulator * sign;
}

[[maybe_unused]] uint16_t strtime_to_minutes(const char* time_str) {
    if (std::strlen(time_str) != 5 || time_str[2] != ':') { // NOLINT(*-magic-numbers)
        return 0xFFFF; // NOLINT(*-magic-numbers)
    }

    const uint16_t hours = (time_str[0] - '0') * 10 + (time_str[1] - '0'); // NOLINT(*-magic-numbers)
    const uint16_t minutes = (time_str[3] - '0') * 10 + (time_str[4] - '0'); // NOLINT(*-magic-numbers)

    return hours * 60 + minutes; // NOLINT(*-magic-numbers)
}

const char* get_mime_type(const String& filename){
    if(server.hasArg("download")) return "application/octet-stream";
    if(filename.endsWith(".htm")) return "text/html";
    if(filename.endsWith(".html")) return "text/html";
    if(filename.endsWith(".css")) return "text/css";
    if(filename.endsWith(".js")) return "application/javascript";
    if(filename.endsWith(".png")) return "image/png";
    if(filename.endsWith(".gif")) return "image/gif";
    if(filename.endsWith(".jpg")) return "image/jpeg";
    if(filename.endsWith(".ico")) return "image/x-icon";
    if(filename.endsWith(".xml")) return "text/xml";
    if(filename.endsWith(".pdf")) return "application/x-pdf";
    if(filename.endsWith(".zip")) return "application/x-zip";
    if(filename.endsWith(".gz")) return "application/x-gzip";
    return "text/plain";
}

void set_led(bool state, size_t transition) {
    led_state = state;
    led_transition_delay = transition / 0xFF;
    PT_SCHEDULE_RESUME(led_control);
}

void wifi_disconnect_cb(const WiFiEventStationModeDisconnected& /*event*/){
    PT_SCHEDULE_RESUME(network_manager);
    ++tries_ctr;
}

void wifi_connected_cb(const WiFiEventStationModeConnected& /*event*/){
    tries_ctr = 0;
}

entry_t parse_task_json(const String& json) {
    // Сохраним индексы, чтобы избежать повторных вычислений
    int start_index = json.indexOf("start_time") + 12;
    int end_index = json.indexOf(',', start_index);
    const long start_time = json.substring(start_index, end_index).toInt();

    start_index = json.indexOf("end_time") + 10;
    end_index = json.indexOf(',', start_index);
    const long end_time = json.substring(start_index, end_index).toInt();

    start_index = json.indexOf("smooth_transition") + 19;
    end_index = json.indexOf(',', start_index);
    const bool smooth_transition = json.substring(start_index, end_index) == "true";

    start_index = json.indexOf("duration") + 10;
    end_index = json.indexOf('}', start_index);
    const long duration = json.substring(start_index, end_index).toInt();

    return {static_cast<uint16_t>(start_time),
            static_cast<uint16_t>(end_time),
            static_cast<uint16_t>(duration)};
}

String create_json(const entry_t* entries, size_t count) {
    String json = "[";

    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            json += ",";  // Добавляем запятую перед каждой следующей записью
        }

        json += "{";
        json += "\"start_time\":" + String(entries[i].start) + ",";
        json += "\"end_time\":" + String(entries[i].end) + ",";
        json += "\"smooth_transition\":" + (entries[i].transition_time ? String("true") : String("false")) + ",";  // Преобразуем в JSON
        json += "\"duration\":" + String(entries[i].transition_time);  // Преобразуем в JSON
        json += "}";
    }

    json += "]";
    return json;  // Возвращаем сформированную строку JSON
}

void web_add_record_cb() {
    const auto payload = server.arg("plain");
    LOG_DEBUG("Request", server.uri(), payload);
    server.send(200, "application/json", payload);
    insert_node(parse_task_json(payload));
}

void web_get_record_cb() {
    const String json_out{create_json(cache_entries, cache_ctr)};
    LOG_DEBUG("Request", server.uri(), json_out);
    server.send(200, "application/json", json_out.c_str());
    return;


    //auto const   entry = cache_entries[0];
    const auto entry = get_node(0);
    const char *fmt = R"([{
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

    String buffer(static_cast<const char *>(nullptr)); //TODO:чтобы не выделял память в куче зря. SSO?
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
}

void web_index_cb() {

    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

PT_THREAD(network_manager) {
    static async_wait delay(0);
    PT_BEGIN(pt);
                while (true) {
                    PT_YIELD_UNTIL(pt, !delay);

                    if (WiFi.status() == WL_CONNECTED) {
                        Serial.print("Connected[");
                        Serial.print(SSID);
                        Serial.print("]");
                        PT_EXIT(pt);
                    }

                    if (tries_ctr >= MAX_WIFI_TRIES) {
                        Serial.println("Too many tries. Timeout");
                        tries_ctr = 0;
                        WiFi.disconnect();
                        delay(5 * 60 * 1000);
                    } else {
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

PT_THREAD(web_server) {
    PT_BEGIN(pt);
                while (true) {
                    server.handleClient();
                    PT_YIELD(pt);
                }
    PT_END(pt);
}

PT_THREAD(led_control){
    static uint8_t i = 0xFF;
    static bool current_state = false;
    static async_wait delay{0};
    if(i == 0xFF) {
        if(led_state == current_state)
            PT_EXIT(pt);
        delay(led_transition_delay);
        i = 0;
    }

    //Serial.printf("LED VALUE: %d#%d\n", i,__LINE__);
    PT_BEGIN(pt);
                for (; i < 0xFF; ++i) {
                    // Serial.printf("LED VALUE: %d#%d\n", i,__LINE__);
                    analogWrite(D5, current_state ? 0xFF - i : i);
                    PT_YIELD_UNTIL(pt, !delay);
                    delay(led_transition_delay);
                    Serial.printf("LED VALUE: %d#%d\n", i, current_state);
                }
//                if(led_state == current_state) {
//                    current_state = !current_state;
//                    PT_YIELD(pt);
//                }
                //  Serial.printf("LED VALUE: %d#%d\n", i,__LINE__);
                current_state = led_state;
                //PT_EXIT(pt);
    PT_END(pt);
}

PT_THREAD(entries_processing){
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