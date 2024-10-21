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
#include "wl_definitions.h"

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
        uint16_t start: 11 ;//: 11;
        uint16_t end: 11; //: 11;
        uint8_t transition_time ;//: 9;
        //service vars
        uint16_t next_node:12;
        uint8_t deleted:1;
        //entry_t* next;
        //uint8_t deleted;// : 1;
        //uint8_t deleted : 1;
    } __attribute__((packed));

    bool led_state = false;
    uint8_t tries_ctr = 0;
    uint32_t led_transition_delay = 0;
    entry_t cache_entries[CACHE_SIZE]{};
    uint8_t cache_ctr = 0;
    ESP8266WebServer server(WEB_PORT);
} // namespace

[[maybe_unused]] static int32_t str_to_int(const char* str);

[[maybe_unused]] static uint16_t strtime_to_minutes(const char* time_str);
static const char* get_mime_type(const String& filename);
static entry_t parse_task_json(const String& json);
//String create_json(const entry_t* entries, size_t count);
String create_json();
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
entry_t get_node(uint16_t address) {
    uint8_t buffer[eeprom::PAGE_SIZE];
    size_t page = address / eeprom::PAGE_SIZE;
    size_t offset = address % eeprom::PAGE_SIZE;

    // Ограничение на выход за пределы EEPROM
    if (address + sizeof(entry_t) > eeprom::STORAGE_SIZE) {
        LOG_ERROR("Address out of bounds");
        return {};
    }

    eeprom::read_random(page * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

    // Перебираем данные внутри страницы
    for (size_t j = offset / sizeof(entry_t); j < eeprom::PAGE_SIZE / sizeof(entry_t); ++j) {
        auto* current_element = reinterpret_cast<entry_t*>(buffer + j * sizeof(entry_t));

        // Проверяем, что запись не удалена
        if (!current_element->deleted) {
            LOG_DEBUG("Node found", current_element->start, current_element->end, current_element->transition_time);
            return *current_element;
        }
    }

    LOG_INFO("EEPROM is empty or all nodes are deleted");
    return {};
}

//TODO: Оптимизация числа записи и чтения
//TODO: Проверка предлов страницы
void insert_node(const entry_t& entry) {
    uint8_t buffer[eeprom::PAGE_SIZE];

    for (size_t i = 0; i < eeprom::STORAGE_SIZE / eeprom::PAGE_SIZE; ++i) {
        eeprom::read_random(i * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

        // Перебираем элементы внутри страницы
        for (size_t j = 0; j < eeprom::PAGE_SIZE / sizeof(entry_t); ++j) {
            const size_t current_address = i * eeprom::PAGE_SIZE + j * sizeof(entry_t);
            entry_t* current_element = reinterpret_cast<entry_t*>(buffer + j * sizeof(entry_t));

            // Если запись удалена, вставляем на её место и сохраняем цепочку
            if (current_element->deleted) {
                LOG_DEBUG("Inserting node into deleted slot", entry.start, entry.end, entry.transition_time);

                // Создаем копию нового элемента
                entry_t new_entry = entry;

                // Сохраняем цепочку — если у удаленного элемента есть указатель на следующий элемент,
                // мы должны перенести этот указатель в новую запись
                new_entry.next_node = current_element->next_node;

                // Записываем новый элемент на место удаленного
                eeprom::write_page(current_address, reinterpret_cast<const uint8_t*>(&new_entry), sizeof(entry_t));
                return;
            }

            // Если нашли последнюю запись в цепочке, добавляем новую
            if (current_element->next_node == 0x0000) {
                LOG_DEBUG("Inserting node into eeprom", entry.start, entry.end, entry.transition_time);

                const size_t next_address = current_address + sizeof(entry_t);

                // Указываем адрес следующего элемента в текущем элементе
                current_element->next_node = next_address;

                // Обновляем текущий элемент с новым указателем next_node
                eeprom::write_page(current_address, reinterpret_cast<const uint8_t*>(current_element), sizeof(entry_t));

                // Записываем новую запись на следующее место
                eeprom::write_page(next_address, reinterpret_cast<const uint8_t*>(&entry), sizeof(entry_t));
                return;
            }
        }
    }

    LOG_INFO("EEPROM is full, unable to insert node");
}


std::vector<entry_t> get_all_nodes() {
    std::vector<entry_t> nodes;  // Для хранения всех считанных элементов
    uint8_t buffer[eeprom::PAGE_SIZE];

    // Начинаем с первого элемента
    size_t current_address = 0;

    while (current_address < eeprom::STORAGE_SIZE) {
        LOG_DEBUG("Reading EEPROM at address", current_address);
        // Определяем страницу и смещение в ней
        size_t page = current_address / eeprom::PAGE_SIZE;
        size_t offset_in_page = current_address % eeprom::PAGE_SIZE;

        // Читаем текущую страницу EEPROM
        eeprom::read_random(page * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

        // Читаем элемент по текущему адресу
        entry_t* current_element = reinterpret_cast<entry_t*>(buffer + offset_in_page);

        // Проверяем, что элемент не удален
        if (!current_element->deleted) {
            // Добавляем элемент в список
            nodes.push_back(*current_element);
        }

        // Если next_node равен 0x0000, значит, это последний элемент
        if (current_element->next_node == 0x0000) {
            break;
        }

        // Переходим к следующему элементу по указателю next_node
        current_address = current_element->next_node;
    }

    return nodes;  // Возвращаем все найденные элементы
}
void delete_node(uint16_t address) {
    uint8_t buffer[eeprom::PAGE_SIZE];
    size_t page = address / eeprom::PAGE_SIZE;
    size_t offset = address % eeprom::PAGE_SIZE;

    // Чтение страницы
    eeprom::read_random(page * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

    entry_t* current_element = reinterpret_cast<entry_t*>(buffer + offset);

    // Если элемент не удален, помечаем его как удаленный
    if (!current_element->deleted) {
        current_element->deleted = 1;
        LOG_DEBUG("Node deleted", current_element->start, current_element->end);
        eeprom::write_page(page * eeprom::PAGE_SIZE + offset, reinterpret_cast<uint8_t*>(current_element), sizeof(entry_t));
    }
    else {
        LOG_INFO("Node already deleted");
    }
}
ntp::ntp_client client("ntp3.time.in.ua");
//ntp::ntp_client client("pool.ntp.org");
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
    client.set_update_interval(60);
    client.set_timezone_offset(3);
    client.set_timeout(1000);
    //uint8_t empty_data[eeprom::PAGE_SIZE];
//    std::fill_n(empty_data, eeprom::PAGE_SIZE, 0x00);  // Заполняем буфер значением 0xFF (или 0x00, если нужно "обнулить")
//
//    // Проходим по всей памяти EEPROM, записывая пустые данные
//    for (size_t i = 0; i < eeprom::STORAGE_SIZE / eeprom::PAGE_SIZE; ++i) {
//        eeprom::write_page(i * eeprom::PAGE_SIZE, empty_data, eeprom::PAGE_SIZE);
//    }
}


void loop() {
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
//    delay(5000);           // wait 5 seconds for next scan
    PT_SCHEDULE(led_control);
    PT_SCHEDULE(network_manager);
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    PT_SCHEDULE(entries_processing);
    client.sync_poll();
    server.handleClient();
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
            static_cast<uint8_t>(duration)};
}

String create_json() {
    String json = "[";

    std::vector<entry_t> all_nodes = get_all_nodes();
    bool flag = false;
    for (const auto& tmp : all_nodes) {
        if (flag) {
            json += ",";  // Добавляем запятую перед каждой следующей записью
        }
        json += "{";
        json += "\"start_time\":" + String(tmp.start) + ",";
        json += "\"end_time\":" + String(tmp.end) + ",";
        json += "\"smooth_transition\":" + (tmp.transition_time ? String("true") : String("false")) + ",";  // Преобразуем в JSON
        json += "\"duration\":" + String(tmp.transition_time);  // Преобразуем в JSON
        json += "}";

        flag = true;
    }

//    uint16_t address = 0x0000;
//    while(true){
//        entry_t const tmp = get_node(address);
//
//        if(tmp.start == 0 && tmp.end == 0) {
//            if(tmp.next_node == 0x0000)
//                break;
//            address = tmp.next_node;
//            continue;
//        }
//        if (address > 0) {
//            json += ",";  // Добавляем запятую перед каждой следующей записью
//        }
//
//        json += "{";
//        json += "\"start_time\":" + String(tmp.start) + ",";
//        json += "\"end_time\":" + String(tmp.end) + ",";
//        json += "\"smooth_transition\":" + (tmp.transition_time ? String("true") : String("false")) + ",";  // Преобразуем в JSON
//        json += "\"duration\":" + String(tmp.transition_time);  // Преобразуем в JSON
//        json += "}";
//        if(tmp.next_node == 0x0000)
//            break;
//        address = tmp.next_node;
//    }
//    for (size_t i = 0; i < count; ++i) {
//        if (i > 0) {
//            json += ",";  // Добавляем запятую перед каждой следующей записью
//        }
//
//        json += "{";
//        json += "\"start_time\":" + String(entries[i].start) + ",";
//        json += "\"end_time\":" + String(entries[i].end) + ",";
//        json += "\"smooth_transition\":" + (entries[i].transition_time ? String("true") : String("false")) + ",";  // Преобразуем в JSON
//        json += "\"duration\":" + String(entries[i].transition_time);  // Преобразуем в JSON
//        json += "}";
//    }

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
    const String json_out{create_json()};
    LOG_DEBUG("Request", server.uri(), json_out);
    server.send(200, "application/json", json_out.c_str());
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

    PT_BEGIN(pt);
                for (; i < 0xFF; ++i) {
                    analogWrite(D5, current_state ? 0xFF - i : i);
                    PT_YIELD_UNTIL(pt, !delay);
                    delay(led_transition_delay);
                    Serial.printf("LED VALUE: %d#%d\n", i, current_state);
                }
                current_state = led_state;
    PT_END(pt);
}

PT_THREAD(entries_processing){
    if(!client.time()){
        return 3;
    }
    Serial.println(ds1307::time(0));
    const std::time_t time_tmp = client.time().value();
    const auto curr_min = std::gmtime(&time_tmp)->tm_hour * 60 + std::gmtime(&time_tmp)->tm_min;
    PT_BEGIN(pt);
                while(true){
                    for(size_t i = 0; i < cache_ctr; ++i){
                        if(cache_entries[i].start == 0 && cache_entries[i].end == 0)
                            continue;
                        // TODO(kandu): оптимизировать/переписать/помечать задачу как запущенную или отправлять в конец списка
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