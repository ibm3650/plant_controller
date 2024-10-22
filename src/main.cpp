#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <ctime>
#include <vector>
#include <queue>
#include <set>
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

typedef struct {
    bool locked;
} pt_mutex_t;

/**
 * Инициализация мьютекса.
 * Устанавливает мьютекс в "разблокированное" состояние.
 */
#define PT_MUTEX_DECL(name) static pt_mutex_t name##_mutex{false};
//#define PT_MUTEX_INIT(mutex) ((mutex)->locked = false)

/**
 * Блокировка мьютекса.
 * Если мьютекс уже захвачен, протопоток блокируется, пока мьютекс не освободится.
 */
#define PT_MUTEX_LOCK(name)        \
  do {                                  \
    PT_WAIT_UNTIL(pt, !(name##_mutex.locked)); \
    name##_mutex.locked = true;             \
  } while(0)

/**
 * Разблокировка мьютекса.
 * Освобождает мьютекс, позволяя другим протопотокам захватить его.
 */
#define PT_MUTEX_UNLOCK(name)      \
  do {                                  \
    name##_mutex.locked = false;            \
  } while(0)


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

        bool operator<(const entry_t& other) const {
            // Пример сравнения по полю start
            if (start != other.start) {
                return start < other.start;
            }
            // Если start равны, сравниваем по полю end
            if (end != other.end) {
                return end < other.end;
            }
            // Если start и end равны, сравниваем по transition_time
            return transition_time < other.transition_time;
        }
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
void web_add_record_cb(WiFiClient& client);
//TODO:получать все записи
//TODO:оптимальное формирование json
//TODO:использовать по возможности статичекое выделение памяти
//TODO:строковыке литералы во флеш памяти
//TODO:использовать кеш
//TODO:Оптимизировать эту функцию
//void web_get_record_cb();
void web_get_record_cb(WiFiClient& client);
void web_delete_record_cb(WiFiClient& client);
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
PT_THREAD_DECL(clock_manager);
PT_MUTEX_DECL(twi_mutex);
//FIXMW: TWI не потокобезопасен

std::queue<std::pair<WiFiClient,std::function<void(WiFiClient&)> >> taskQueue;



std::set<entry_t> entriesQueue;

void cache(){
    std::vector<std::pair<uint16_t, entry_t>> nodes;  // Для хранения всех считанных элементов
    uint8_t buffer[eeprom::PAGE_SIZE];

    // Начинаем с первого элемента
    uint16_t current_address = 0x0000;

    while (current_address < eeprom::STORAGE_SIZE) {
        LOG_DEBUG("Reading EEPROM at address", current_address);
        // Определяем страницу и смещение в ней
        size_t const page = current_address / eeprom::PAGE_SIZE;
        size_t const offset_in_page = current_address % eeprom::PAGE_SIZE;

        // Читаем текущую страницу EEPROM
        eeprom::read_random(page * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

        // Читаем элемент по текущему адресу
        auto* current_element = reinterpret_cast<entry_t*>(buffer + offset_in_page);

        // Проверяем, что элемент не удален
        if (!current_element->deleted) {
            const std::time_t current_time = ds1307::time(0);
            const auto curr_min = std::localtime(&current_time)->tm_hour * 60 + std::localtime(&current_time)->tm_min;
            if (curr_min >= current_element->start || curr_min <= current_element->end) {
                entriesQueue.insert(*current_element);
                if (entriesQueue.size() >= CACHE_SIZE) {
                    auto lastElement = std::prev(entriesQueue.end());
                    // Удаляем последний элемент
                    entriesQueue.erase(lastElement);
                }
            }
        }

        // Если next_node равен 0x0000, значит, это последний элемент
        if (current_element->next_node == 0x0000) {
            break;
        }

        // Переходим к следующему элементу по указателю next_node
        current_address = current_element->next_node;
    }

   // return nodes;  // Возвращаем все найденные элементы
}



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


std::vector<std::pair<uint16_t, entry_t>> get_all_nodes() {
    std::vector<std::pair<uint16_t, entry_t>> nodes;  // Для хранения всех считанных элементов
    uint8_t buffer[eeprom::PAGE_SIZE];

    // Начинаем с первого элемента
    uint16_t current_address = 0x0000;

    while (current_address < eeprom::STORAGE_SIZE) {
        LOG_DEBUG("Reading EEPROM at address", current_address);
        // Определяем страницу и смещение в ней
        size_t const page = current_address / eeprom::PAGE_SIZE;
        size_t const offset_in_page = current_address % eeprom::PAGE_SIZE;

        // Читаем текущую страницу EEPROM
        eeprom::read_random(page * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

        // Читаем элемент по текущему адресу
        auto* current_element = reinterpret_cast<entry_t*>(buffer + offset_in_page);

        // Проверяем, что элемент не удален
        if (!current_element->deleted) {
            // Добавляем элемент в список
            nodes.emplace_back(current_address, *current_element);
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
        LOG_DEBUG("Node deleted", address, current_element->start, current_element->end);
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
    server.on("/add_record", [&]() {
        taskQueue.emplace(server.client(), web_add_record_cb);
        //WiFiClient client = server.client();
        //FIXME: Что значит mutable в контексте лямбда функции
////        auto task = [client]() mutable {
////            client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
////        };
    });
    //server.on("/add_record", web_add_record_cb);
   // server.on("/get_records",  web_get_record_cb);
    server.on("/get_records",  [&]() {
        taskQueue.emplace(server.client(), web_get_record_cb);
    });
    //server.on("/delete_record",  web_delete_record_cb);
    server.on("/delete_record",  [&]() {
        taskQueue.emplace(server.client(), web_delete_record_cb);
    });
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
    //server.keepAlive(true);
    //server.keepAlive(true);
    server.begin();
    client.set_update_interval(60);
    client.set_timezone_offset(3);
    client.set_timeout(1000);
    //cache();
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
    //LOG_DEBUG("Clock manager");
    PT_SCHEDULE(clock_manager);
    //delay(50);
   // LOG_DEBUG("Entries processing");
    PT_SCHEDULE(entries_processing);
    delay(20);//FIXME: Почему так много? Без него веб-сервер не работает нормально. Вероятно из-за того, что не успевает обработать запросы и планировщику нужно время на обработку
   // LOG_DEBUG("Web server");
    PT_SCHEDULE(web_server);
    //LOG_DEBUG("Async sync poll");
    client.sync_poll();
    //LOG_DEBUG("handleClient");
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
    LOG_DEBUG("Creating JSON");
    String json = "[";

    const auto all_nodes = get_all_nodes();
    bool is_first = false;
    for (const auto& [address, node] : all_nodes) {
        if (is_first) {
            json += ",";  // Добавляем запятую перед каждой следующей записью
        }
        json += "{";
        json += "\"start_time\":" + String(node.start) + ",";
        json += "\"end_time\":" + String(node.end) + ",";
        json += "\"smooth_transition\":" + (node.transition_time ? String("true") : String("false")) + ",";  // Преобразуем в JSON
        json += "\"duration\":" + String(node.transition_time) + ",";
        json += "\"address\":" + String(address);  // Преобразуем в JSON
        json += "}";

        is_first = true;
    }
    json += "]";
    return json;  // Возвращаем сформированную строку JSON
}
//String create_json() {
//    LOG_DEBUG("Creating JSON");
//    String json = "[";
//
//    const auto all_nodes = get_all_nodes();
//    bool is_first = false;
//    for (const auto& node: entriesQueue) {
//        if (is_first) {
//            json += ",";  // Добавляем запятую перед каждой следующей записью
//        }
//        json += "{";
//        json += "\"start_time\":" + String(node.start) + ",";
//        json += "\"end_time\":" + String(node.end) + ",";
//        json += "\"smooth_transition\":" + (node.transition_time ? String("true") : String("false")) + ",";  // Преобразуем в JSON
//        json += "\"duration\":" + String(node.transition_time) + ",";
//        json += "\"address\":" + String(0x0000);  // Преобразуем в JSON
//        json += "}";
//
//        is_first = true;
//    }
//    json += "]";
//    return json;  // Возвращаем сформированную строку JSON
//}
void web_add_record_cb(WiFiClient& client) {
    const auto payload = server.arg("plain");
    LOG_DEBUG("Request", server.uri(), payload);
    server.send(200, "application/json", payload);
    insert_node(parse_task_json(payload));
}


void web_delete_record_cb(WiFiClient& client) {
    const auto payload = server.arg("plain");
    LOG_DEBUG("Request", server.uri(), payload, payload.substring(payload.indexOf(':') + 1, payload.length() - 1));
    server.send(200, "application/json", payload);
    delete_node(str_to_int(payload.substring(payload.indexOf(':') + 1, payload.length() - 1).c_str()));
}

void web_get_record_cb(WiFiClient& client) {
    LOG_DEBUG("Request", __FUNCTION__ , client.remoteIP().toString());
    const String json_out{create_json()};
    LOG_DEBUG("Request JSON", __FUNCTION__ , json_out);


    // Отправка ответа
    client.print("HTTP/1.1 200 OK\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(json_out.length());
    client.print("\r\n\r\n");
    client.print(json_out);
    //server.send(200, "application/json", json_out.c_str());
}

void web_index_cb() {
    LOG_DEBUG("Request", __FUNCTION__ , server.uri());
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
    WiFiClient client1;
    std::function<void(WiFiClient&)> functor;
    PT_BEGIN(pt);
                while (true) {
                    PT_YIELD_UNTIL(pt, !taskQueue.empty());
                    LOG_DEBUG("New task count:", __FUNCTION__ , taskQueue.size());
                    //auto [client, functor] = taskQueue.front();
                    //[client, functor] = taskQueue.front();
                    client1 = taskQueue.front().first;
                    functor = taskQueue.front().second;
                    taskQueue.pop();
                  //  PT_MUTEX_LOCK(twi_mutex);
                    functor(client1);
                  //  PT_MUTEX_UNLOCK(twi_mutex);
                  //  PT_MUTEX_LOCK(twi_mutex);
                    //server.handleClient();
                   // PT_MUTEX_UNLOCK(twi_mutex);
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
    std::time_t time_tmp;
    uint16_t curr_min;
    std::set<entry_t>::iterator item;
    static async_wait delay(0);
    PT_BEGIN(pt);

                while(true){
                    if(entriesQueue.empty()){
                        cache();
                        if(entriesQueue.empty()) {
                            PT_YIELD(pt);
}
                    }
                    //PT_MUTEX_LOCK(twi_mutex);
                    time_tmp = ds1307::time(0);
                   // Serial.println(time_tmp);
                  //  PT_MUTEX_UNLOCK(twi_mutex);
                    if(time_tmp == -1) {
                        //return 3;
                        PT_YIELD(pt);
}
                    LOG_DEBUG("IN cache", entriesQueue.size());
                    Serial.println(time_tmp);
                    curr_min = std::localtime(&time_tmp)->tm_hour * 60 + std::localtime(&time_tmp)->tm_min;
                    item = entriesQueue.begin();
                    LOG_DEBUG("Current time", curr_min, "Current item", item->start, item->end);
                    if(curr_min >= item->end){
                        entriesQueue.erase(item);
                    }
                    else if (curr_min >= (item->end - item->transition_time)) {
                        set_led(false, item->transition_time * 60 * 1000);
                    }
                    else if (curr_min >= item->start) {
                        set_led(true, item->transition_time * 60 * 1000);
                    }
                    PT_YIELD_UNTIL(pt, !delay);
                    delay(60 * 1000);
                    PT_YIELD(pt);
                }
    PT_END(pt);
}

PT_THREAD(clock_manager){
    static async_wait delay(0);
    PT_BEGIN(pt);
                while(true){
                   // PT_MUTEX_LOCK(twi_mutex);
                    if(!client.time()){
                        LOG_ERROR("NTP Failed ");
                        PT_YIELD(pt);
                    }
                    if(!ds1307::is_enabled()){
                        LOG_ERROR("DS1307 is not enabled");
                        ds1307::set_oscilator(true);

                    }
                 //   PT_MUTEX_UNLOCK(twi_mutex);
                    PT_YIELD_UNTIL(pt, !delay);
                    delay(12 * 60 * 60 * 1000);
                   // PT_MUTEX_LOCK(twi_mutex);
                    ds1307::set_time(client.time().value());
                  //  PT_MUTEX_UNLOCK(twi_mutex);
                    LOG_INFO("Time updated");
                    PT_YIELD(pt);
                }
    PT_END(pt);
}