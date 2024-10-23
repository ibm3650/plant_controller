#include "async_wait.h"
#include "debug.h"
#include "ds1307.h"
#include "eeprom_storage.h"
#include "ntp.h"
#include "pt.h"
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <ctime>
#include <queue>
#include <vector>


template<typename T, typename... args_pack>
static void print_variadic(const T &first, args_pack... args) {
    Serial.print(first);
    Serial.print(' ');
    if constexpr (sizeof...(args) > 0) {
        print_variadic(args...);
    } else {
        Serial.println();
    }
}

template<typename... args_pack>
static void log_message(LogLevel level, const char *file, int line, args_pack... message) {
    if (level < CURRENT_LOG_LEVEL) {
        return;
    }

    String level_str;
    switch (level) {
        case LogLevel::DEBUG:
            level_str = "DEBUG";
            break;
        case LogLevel::INFO:
            level_str = "INFO";
            break;
        case LogLevel::WARNING:
            level_str = "WARNING";
            break;
        case LogLevel::ERROR:
            level_str = "ERROR";
            break;
    }

    const uint32_t current_time = millis();  // Время в миллисекундах с момента старта
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


namespace {
    constexpr auto SSID = "MikroTik-B971FF";
    constexpr auto PASSWORD = "pussydestroyer228";
    constexpr uint8_t MAX_WIFI_TRIES = 15;

    constexpr uint16_t WEB_PORT = 80;


    bool led_state = false;
    uint8_t tries_ctr = 0;
    uint8_t pwm_val = 0xFF;
    uint32_t led_transition_delay = 0;
    ESP8266WebServer server(WEB_PORT);
    std::queue<std::pair<WiFiClient, std::function<void(WiFiClient &)> >> task_queue;
    ntp::ntp_client ntp_client("ntp3.time.in.ua");

} // namespace

[[maybe_unused]] static int32_t str_to_int(const char *str);

[[maybe_unused]] static uint16_t strtime_to_minutes(std::string_view time_str);

static const char *get_mime_type(const String &filename);

static entry_t parse_task_json(const String &json);


String create_json();


void set_led(bool state, size_t transition = 0, uint8_t pwm = 0);

void wifi_disconnect_cb(const WiFiEventStationModeDisconnected &event);

void wifi_connected_cb(const WiFiEventStationModeConnected &event);

// TODO(kandu): оптимизация парсинга json
// TODO(kandu): верификация полученного json для корректного ответа
// TODO(kandu): защита от повторной вставки
// TODO(kandu): проверки при добавлении
// TODO(kandu): пв sprintf правильные литералы аргументов или вообще std::format
// TODO(kandu): Оптимистичный ответ
void web_add_record_cb(WiFiClient &client);

// TODO(kandu): получать все записи
// TODO(kandu): оптимальное формирование json
// TODO(kandu): использовать по возможности статическое выделение памяти
// TODO(kandu): строковые литералы во флеш памяти
// TODO(kandu): использовать кеш
// TODO(kandu): Оптимизировать эту функцию
//void web_get_record_cb();
void web_get_record_cb(WiFiClient &client);

void web_delete_record_cb(WiFiClient &client);

void web_index_cb();

PT_THREAD_DECL(network_manager);
// TODO(kandu): Вариант не работает с режимом точки доступа
// TODO(kandu): Не ждет подключения к ТД или поднятия хотспоста
// TODO(kandu): Остановка сервера при простое переподключения
// TODO(kandu): Работа с i2c тоже протопоток
PT_THREAD_DECL(web_server);

PT_THREAD_DECL(led_control);
// TODO(kandu): отвязать от сети
PT_THREAD_DECL(entries_processing);
// TODO(kandu): Первый байт на страницу EEPROM обозначает, если ли в этой странице свободные ячейки, например после удаления узла из списка, для дефрагментации и эффективной вставки. такие блоки ищутся перед вставкой

// TODO(kandu): полная реализация time по стандарту rfc
PT_THREAD_DECL(clock_manager);











//ntp::ntp_client ntp_client("pool.ntp.org");

void setup() {
    Serial.begin(115200);
    Wire.begin();
    SPIFFS.begin();
    pinMode(D5, OUTPUT);
    WiFi.onStationModeConnected(wifi_connected_cb);
    WiFi.onStationModeDisconnected(wifi_disconnect_cb);
    server.on("/add_record", [&]() {
        task_queue.emplace(server.client(), web_add_record_cb);
        //WiFiClient ntp_client = server.ntp_client();
        //FIXME: Что значит mutable в контексте лямбда функции
////        auto task = [ntp_client]() mutable {
////            ntp_client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
////        };
    });
    //server.on("/add_record", web_add_record_cb);
    // server.on("/get_records",  web_get_record_cb);
    server.on("/get_records", [&]() {
        task_queue.emplace(server.client(), web_get_record_cb);
    });
    //server.on("/delete_record",  web_delete_record_cb);
    server.on("/delete_record", [&]() {
        task_queue.emplace(server.client(), web_delete_record_cb);
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
    ntp_client.set_update_interval(60);
    ntp_client.set_timezone_offset(3);
    ntp_client.set_timeout(1000);
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
    PT_SCHEDULE(led_control);
    PT_SCHEDULE(network_manager);
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    PT_SCHEDULE(clock_manager);
    PT_SCHEDULE(entries_processing);
    delay(20);//FIXME: Почему так много? Без него веб-сервер не работает нормально. Вероятно из-за того, что не успевает обработать запросы и планировщику нужно время на обработку
    PT_SCHEDULE(web_server);
    ntp_client.sync_poll();
    server.handleClient();
}

[[maybe_unused]] int32_t str_to_int(const char *str) {
    int8_t sign = 1;
    if (*str == '-') {
        str++;
        sign = -1;
    }
    int32_t accumulator = 0;
    while (*str) {
        if (*str < '0' || *str > '9') {
            return 0;
        }
        assert(*str >= '0' && *str <= '9');
        accumulator = accumulator * 10 + (*str - '0'); // NOLINT(*-magic-numbers)
        str++;
    }
    return accumulator * sign;
}

[[maybe_unused]] uint16_t strtime_to_minutes(std::string_view time_str) {
    if (time_str.length() != 5 || time_str[2] != ':') {
        return 0xFFFF;
    }

    const uint16_t hours = ((time_str[0] - '0') * 10) + (time_str[1] - '0');
    const uint16_t minutes = ((time_str[3] - '0') * 10) + (time_str[4] - '0');

    return (hours * 60) + minutes;
}

const char *get_mime_type(const String &filename) {
    if (server.hasArg("download")) {
        return "application/octet-stream";
    }
    if (filename.endsWith(".htm")) {
        return "text/html";
    }
    if (filename.endsWith(".html")) {
        return "text/html";
    }
    if (filename.endsWith(".css")) {
        return "text/css";
    }
    if (filename.endsWith(".js")) {
        return "application/javascript";
    }
    if (filename.endsWith(".png")) {
        return "image/png";
    }
    if (filename.endsWith(".gif")) {
        return "image/gif";
    }
    if (filename.endsWith(".jpg")) {
        return "image/jpeg";
    }
    if (filename.endsWith(".ico")) {
        return "image/x-icon";
    }
    if (filename.endsWith(".xml")) {
        return "text/xml";
    }
    if (filename.endsWith(".pdf")) {
        return "application/x-pdf";
    }
    if (filename.endsWith(".zip")) {
        return "application/x-zip";
    }
    if (filename.endsWith(".gz")) {
        return "application/x-gzip";
    }
    return "text/plain";
}

void set_led(bool state, size_t transition, uint8_t pwm) {
    led_state = state;
    led_transition_delay = transition / 0xFF;
    pwm_val = pwm;
    PT_SCHEDULE_RESUME(led_control);
}

void wifi_disconnect_cb(const WiFiEventStationModeDisconnected & /*event*/) {
    PT_SCHEDULE_RESUME(network_manager);
    ++tries_ctr;
}

void wifi_connected_cb(const WiFiEventStationModeConnected & /*event*/) {
    tries_ctr = 0;
}

entry_t parse_task_json(const String &json) {
    // Сохраним индексы, чтобы избежать повторных вычислений
    int start_index = json.indexOf("start_time") + 12;
    int end_index = json.indexOf(',', start_index);
    const int32_t start_time = json.substring(start_index, end_index).toInt();

    start_index = json.indexOf("end_time") + 10;
    end_index = json.indexOf(',', start_index);
    const int32_t end_time = json.substring(start_index, end_index).toInt();

    start_index = json.indexOf("smooth_transition") + 19;
    end_index = json.indexOf(',', start_index);
    //const bool smooth_transition = json.substring(start_index, end_index) == "true";

    start_index = json.indexOf("duration") + 10;
    end_index = json.indexOf('}', start_index);
    const int32_t duration = json.substring(start_index, end_index).toInt();

    return {static_cast<uint16_t>(start_time),
            static_cast<uint16_t>(end_time),
            static_cast<uint8_t>(duration)};
}

String create_json() {
    LOG_DEBUG("Creating JSON");
    String json = "[";

    const auto all_nodes = get_all_nodes();
    bool is_first = false;
    for (const auto &[address, node]: all_nodes) {
        if (is_first) {
            json += ",";  // Добавляем запятую перед каждой следующей записью
        }
        json += "{";
        json += "\"start_time\":" + String(node.start) + ",";
        json += "\"end_time\":" + String(node.end) + ",";
        json += "\"smooth_transition\":" + ((node.transition_time != 0u) ? String("true") : String("false")) +
                ",";  // Преобразуем в JSON
        json += "\"duration\":" + String(node.transition_time) + ",";
        json += "\"address\":" + String(address);  // Преобразуем в JSON
        json += "}";

        is_first = true;
    }
    json += "]";
    return json;  // Возвращаем сформированную строку JSON
}


void web_add_record_cb(WiFiClient &client) {
    const auto payload = server.arg("plain");
    LOG_DEBUG("Request", server.uri(), payload);
    client.print("HTTP/1.1 200 OK\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(payload.length());
    client.print("\r\n\r\n");
    client.print(payload);
    //server.send(200, "application/json", payload);
    insert_node(parse_task_json(payload));
}


void web_delete_record_cb(WiFiClient &client) {
    const auto payload = server.arg("plain");
    LOG_DEBUG("Request", server.uri(), payload, payload.substring(payload.indexOf(':') + 1, payload.length() - 1));
    // server.send(200, "application/json", payload);
    client.print("HTTP/1.1 200 OK\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(payload.length());
    client.print("\r\n\r\n");
    client.print(payload);
    delete_node(str_to_int(payload.substring(payload.indexOf(':') + 1, payload.length() - 1).c_str()));
}

void web_get_record_cb(WiFiClient &client) {
    LOG_DEBUG("Request", __FUNCTION__, client.remoteIP().toString());
    const String json_out{create_json()};
    LOG_DEBUG("Request JSON", __FUNCTION__, json_out);


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
    LOG_DEBUG("Request", __FUNCTION__, server.uri());
    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
        return;
    }

    server.streamFile(file, "text/html");
    file.close();
}

PT_THREAD(network_manager) {
    static async_wait delay(0);
    PT_BEGIN(thread_context);
            while (true) {
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.print("Connected[");
                    Serial.print(SSID);
                    Serial.print("]");
                    PT_STOP(thread_context);
                }

                if (tries_ctr >= MAX_WIFI_TRIES) {
                    Serial.println("Too many tries. Timeout");
                    tries_ctr = 0;
                    WiFi.disconnect();
                    PT_WAIT(thread_context, 5 * 60 * 1000);
                } else {
                    if (tries_ctr++ == 0)
                        WiFi.begin(SSID, PASSWORD);
                    Serial.print("Connecting to WIFI [");
                    Serial.print(SSID);
                    Serial.print("] #");

                    Serial.println(tries_ctr);
                    PT_WAIT(thread_context, 1 * 1000);
                }
            }
    PT_END(thread_context);
}

PT_THREAD(web_server) {
    WiFiClient client1;
    std::function<void(WiFiClient &)> functor;
    PT_BEGIN(thread_context);
            while (true) {
                PT_YIELD_WHILE(thread_context, task_queue.empty());
                LOG_DEBUG("New task count:", __FUNCTION__, task_queue.size());
                client1 = task_queue.front().first;
                functor = task_queue.front().second;
                task_queue.pop();
                functor(client1);
            }
    PT_END(thread_context);
}

PT_THREAD(led_control) {

    static bool current_state = false;
    static async_wait delay{0};


    PT_BEGIN(thread_context);
            if (pwm_val == 0xFF) {
                if (led_state == current_state)
                    PT_STOP(thread_context);
                // PT_EXIT(pt);
                delay(led_transition_delay);
                pwm_val = 0;
            } else {
                if (led_state == current_state)
                    current_state = !current_state;
            }
            for (; pwm_val < 0xFF; ++pwm_val) {
                analogWrite(D5, current_state ? 0xFF - pwm_val : pwm_val);
                PT_WAIT(thread_context, led_transition_delay);
                Serial.printf("LED VALUE: %d#%d\n", pwm_val, current_state);
                PT_YIELD(thread_context);
            }
            current_state = led_state;
    PT_END(thread_context);
}

PT_THREAD(entries_processing) {
    std::time_t time_tmp = 0;
    uint32_t curr_min = 0;
    entry_t item{};
    static async_wait delay(0);
    static bool is_processing = false;
    PT_BEGIN(thread_context);

            while (true) {
                if (is_cache_empty()) {
                    cache();
                    //LOG_DEBUG("Cache updated", entriesQueue.size());
                    if (is_cache_empty()) {
                        LOG_DEBUG("No entries in cache");
                        PT_WAIT(thread_context, 60 * 1000);
                        PT_EXIT(thread_context);
                    }
                }
                time_tmp = ds1307::time(0);
                if (time_tmp == -1) {
                    PT_EXIT(thread_context);
                }

                //FIXME:При добавлении элемента обновлять кеш
                //LOG_DEBUG("IN cache", entriesQueue.size());
                Serial.println(time_tmp);
                curr_min = std::localtime(&time_tmp)->tm_hour * 60 + std::localtime(&time_tmp)->tm_min;
                item = cache_top();
                LOG_DEBUG("Current time", curr_min, "Current item", item.start, item.end);
                if (curr_min >= item.end) {
                    cache_pop();
                    is_processing = false;
                } else if (curr_min >= static_cast<uint32_t>(item.end - item.transition_time)) {
                    if (!is_processing) {
                        auto led_transition_delay3 = item.transition_time * 60 * 1000 / 0xFF;
                        curr_min -= item.end - item.transition_time;
                        if (curr_min >= item.transition_time) {
                            set_led(false);
                        } else {
                            curr_min *= 60 * 1000;
                            curr_min /= led_transition_delay3;
                            set_led(false, item.transition_time * 60 * 1000, curr_min);
                        }
                        is_processing = true;
                    }
                } else if (curr_min >= item.start) {
                    if (!is_processing) {
                        auto led_transition_delay3 = item.transition_time * 60 * 1000 / 0xFF;
                        LOG_DEBUG("Transition", led_transition_delay3, item.transition_time);
                        curr_min -= item.start;
                        LOG_DEBUG("Elapsed time", curr_min);
                        if (curr_min >= item.transition_time) {
                            set_led(true);
                        } else {
                            curr_min *= 60 * 1000;
                            curr_min /= led_transition_delay3;
                            LOG_DEBUG("PWM", curr_min);
                            set_led(true, item.transition_time * 60 * 1000, curr_min);
                        }
                        is_processing = true;
                    }
                }
                PT_WAIT(thread_context, 60 * 1000);
            }
    PT_END(thread_context);
}

PT_THREAD(clock_manager) {
    static async_wait delay(0);
    PT_BEGIN(thread_context);
            while (true) {
                if (!ntp_client.time()) {
                    LOG_ERROR("NTP Failed ");
                    PT_EXIT(thread_context);
                }
                if (!ds1307::is_enabled()) {
                    LOG_ERROR("DS1307 is not enabled");
                    ds1307::set_oscilator(true);
                }
                ds1307::set_time(ntp_client.time().value());
                PT_WAIT(thread_context, 12 * 60 * 60 * 1000);
                LOG_INFO("Time updated");
            }
    PT_END(thread_context);
}